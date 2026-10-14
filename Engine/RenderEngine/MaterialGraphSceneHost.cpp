#include "MaterialGraphSceneHost.h"
#include "MaterialGraphGeometryContract.h"
#include "Render/Graph/ShadowMath.h"
#include "MaterialGraphSceneAccess.h"
#include "MaterialGraphSceneCompiler.h"
#include "MaterialGraphIndirectBatch.h"
#include "MaterialGraphWork.h"


#include "PathFinder.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <ranges>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <limits>
#include <cstdio>
#include <stdexcept>

namespace material_graph
{
    namespace
    {
        // Diagnostic only (CREATOR_METEOR_SHADOW_MESHLET_AUDIT=1). Replays the shadow
        // mesh shader's per-meshlet clip rejection (LXSceneMeshletVisible) on the
        // CPU, so the work spent on meshlets that the shader discards is countable.
        bool ShadowMeshletAuditEnabled()
        {
            static const bool enabled = [] {
                char flag[8]{};
                std::size_t bytes{};
                getenv_s(&bytes, flag, sizeof(flag), "CREATOR_METEOR_SHADOW_MESHLET_AUDIT");
                return std::strcmp(flag, "1") == 0;
            }();
            return enabled;
        }

        struct ShadowMeshletAudit
        {
            std::uint64_t draws{}, visibleDraws{}, meshlets{}, rejected{}, triangles{}, rejectedTriangles{};
        };

        // One static (unskinned) meshlet caster against one cascade. The instance
        // test mirrors GPU visibility's sphere/clip rejection before dispatch.
        void AuditShadowMeshlets(const MeshSurfaceInput& input, const math::matrix4x4& lightViewProjection,
                                 ShadowMeshletAudit& audit)
        {
            const auto& geometry = input.Geometry();
            const auto& topology = input.Meshlets();
            if (!topology.HasMeshlets()) return;
            const math::matrix4x4 toClip = input.World() * lightViewProjection;
            const auto& world = input.World();
            const auto vertexCount = geometry.vertexBytes / geometry.vertexStride;
            const auto position = [&](std::uint32_t index) {
                float local[3];
                std::memcpy(local, static_cast<const std::byte*>(geometry.vertexData) + index * geometry.vertexStride,
                            sizeof(local));
                return std::array<float, 3>{local[0], local[1], local[2]};
            };
            const auto transform = [](const std::array<float, 3>& p, const math::matrix4x4& m) {
                std::array<float, 4> result{};
                for (unsigned c = 0; c < 4; ++c)
                {
                    result[c] = p[0] * m.m[0][c] + p[1] * m.m[1][c] + p[2] * m.m[2][c] + m.m[3][c];
                }
                return result;
            };
            // Instance sphere from the world-space extent of all vertices.
            std::array<float, 3> low{}, high{};
            for (std::uint64_t i = 0; i < vertexCount; ++i)
            {
                const auto p = transform(position(static_cast<std::uint32_t>(i)), world);
                for (unsigned axis = 0; axis < 3; ++axis)
                {
                    low[axis] = i ? (std::min)(low[axis], p[axis]) : p[axis];
                    high[axis] = i ? (std::max)(high[axis], p[axis]) : p[axis];
                }
            }
            shadow_math::Sphere sphere;
            sphere.center = {(low[0] + high[0]) * .5f, (low[1] + high[1]) * .5f, (low[2] + high[2]) * .5f};
            sphere.radius = std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) * .5f * 1.0001f + 1e-4f;
            ++audit.draws;
            if (!shadow_math::IntersectsClip(sphere, lightViewProjection))
            {
                return;
            }
            ++audit.visibleDraws;
            const auto meshlets = static_cast<uint32_t>(topology.descriptors.size());
            for (std::uint32_t meshlet = 0; meshlet < meshlets; ++meshlet)
            {
                const auto& descriptor = topology.descriptors[meshlet];
                std::uint32_t outside = 63u;
                for (std::uint32_t lane = 0; lane < descriptor.vertexCount; ++lane)
                {
                    const auto clip = transform(position(topology.vertexRemap[descriptor.vertexOffset + lane]), toClip);
                    const float epsilon = (std::max)(1.f, (std::max)(std::abs(clip[3]), (std::max)(std::abs(clip[0]),
                        (std::max)(std::abs(clip[1]), std::abs(clip[2]))))) * 0.00001f;
                    std::uint32_t mask = 0u;
                    if (std::isfinite(clip[0]) && std::isfinite(clip[1]) && std::isfinite(clip[2]) && std::isfinite(clip[3]))
                    {
                        mask = (clip[0] < -clip[3] - epsilon ? 1u : 0u) | (clip[0] > clip[3] + epsilon ? 2u : 0u) |
                               (clip[1] < -clip[3] - epsilon ? 4u : 0u) | (clip[1] > clip[3] + epsilon ? 8u : 0u) |
                               (clip[2] < -epsilon ? 16u : 0u) | (clip[2] > clip[3] + epsilon ? 32u : 0u);
                    }
                    outside &= mask;
                }
                ++audit.meshlets;
                audit.triangles += descriptor.triangleCount;
                if (outside != 0u)
                {
                    ++audit.rejected;
                    audit.rejectedTriangles += descriptor.triangleCount;
                }
            }
        }

        template<class Left, class Right>
        bool SamePinnedObject(const Left& left, const Right& right)
        {
            if (!left || !right)
            {
                return !left && !right;
            }
            if constexpr (requires { left->representationId; right->representationId; })
            {
                // Both handles are anchored during this check. A copied or
                // forged representation key is not the same immutable instance.
                return InstanceFramePins::Identity(*left) == InstanceFramePins::Identity(*right)
                    && std::addressof(*left) == std::addressof(*right);
            }
            else
            {
                return left->assetId == right->assetId && left->generation == right->generation
                    && left->cooked.product.program.semanticKey == right->cooked.product.program.semanticKey;
            }
        }

        constexpr char PreparationAdmissionError[] =
            "METEOR Scene program preparation exceeds its 64-request admission budget.";
        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        struct SceneConstants
        {
            math::matrix4x4 viewProjection;
            math::vector4 eye;
            std::uint32_t owner{}, coverage{};
            float cutoff{};
            std::uint32_t environment{}, lightCount{}, hasShadow{};
            float shadowBlend{};
            std::uint32_t width{}, height{}, padding[3]{};
            math::matrix4x4 shadowProjection[3];
            math::vector4 shadowSplits, shadowBias, cameraForward;
            EnhancedLight lights[64];
            math::matrix4x4 temporalCurrentProjection, temporalPreviousProjection;
            std::uint32_t temporalHistoryValid{}, temporalPadding[3]{};
            SceneGeometryIdentity geometryIdentity;
        };
        static_assert(sizeof(SceneConstants) == 4672);
        static_assert(offsetof(SceneConstants, lights) == 368);

        struct SceneShadowConstants
        {
            math::matrix4x4 viewProjection;
            std::uint32_t coverage{};
            float cutoff{};
            std::uint32_t padding0{}, padding1{};
            SceneGeometryIdentity geometryIdentity;
        };
        static_assert(sizeof(SceneShadowConstants) == 144);

        std::array<RGHandle, 5> Colors(const SceneSurfaceTargets::Outputs& outputs)
        {
            return {outputs.diffuse, outputs.metalRough, outputs.normal, outputs.emissive, outputs.bitmask};
        }
    } // namespace

    struct SceneHost::Program
    {
        own::shared_owner<const Generation> generation;
        RHIShaderBinary backend{};
        PassLayout layout;
        PassLayout shadowLayout;
        LX::Runtime::GraphicsPipeline shadow;
        std::array<LX::Runtime::GraphicsPipeline, 2> gbuffer, visibilitySurface, depth, color, blendedColor, temporal;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> lookup, blendedLookup;
        std::array<LX::Runtime::GraphicsPipeline, 2> subsurface;
        std::array<LX::Runtime::GraphicsPipeline, 2> refraction;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> runtimeEffects;
        PassLayout volumeLayout;
        LX::Runtime::ComputePipeline volume;
        RHIShaderBlob meshShader, visibilityMeshShader, reconstructShader, shadowMeshShader;
        std::unordered_map<uint32_t, RHIPipelineHandle> reconstructionPipelines;
        std::map<uint64_t, RHIPipelineHandle> meshPipelines;
        uint32_t rasterRouteRoot{}, visibilityKeyRoot{}, meshletRoot{}, temporalPreviousRoot{}, geometryTableRoot{}, visibilityRoot{}, materialWorkRoot{}, shadowMeshletRoot{2};
        bool meshEnabled{};
        bool hasSurface{}, hasVolume{};
        bool hasSubsurface{}, hasTransmission{}, hasSpecial{};
    };

    struct SceneHost::Frame
    {
        struct Draw
        {
            own::shared_owner<const Program> program;
            own::shared_owner<const RenderBindings> bindings;
            bool shadowEnabled{};
            std::shared_ptr<const MeshSurfaceBatch> geometry, previousGeometry;
            RHIBufferSlice indices, constants, referenceConstants;
            RHIBufferSlice visibleOwner;
            std::uint32_t visibilityBin{UINT32_MAX}, visibleIdOffset{};
            std::uint32_t cameraMeshletBin{UINT32_MAX};
            std::uint32_t materialBucket{UINT32_MAX}, materialTileBucket{UINT32_MAX}, geometryRecord{};
            std::uint32_t indexedBucket{UINT32_MAX};
            bool bucketRepresentative{true};

            std::array<RHIBufferSlice, 3> shadowConstants;
            std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> shadowMeshlets;
            std::uint32_t shadowMeshletBin{UINT32_MAX};

            bool doubleSided{}, blended{};
            std::size_t inputIndex{};
        };
        IRenderDeviceServices* device{};
        std::uint64_t recording{}, descriptors{};
        own::shared_owner<const SceneViewInput> input;
        std::shared_ptr<const SceneFrameGeometry> geometryTable;
        std::shared_ptr<const SceneHybridVisibility> hybridVisibility;
        std::vector<Draw> draws;
        std::vector<Draw> shadowDraws;
        std::vector<std::pair<size_t, size_t>> inputRanges;
        std::span<const Draw> DrawsFor(std::optional<size_t> inputIndex) const
        {
            if (!inputIndex)
            {
                return draws;
            }
            if (*inputIndex >= inputRanges.size())
            {
                return {};
            }
            const auto [first, count] = inputRanges[*inputIndex];
            return std::span<const Draw>(draws).subspan(first, count);
        }
        // All passes reference the canonical draw storage. Representatives are a
        // view, never a second owning list with a separately maintained input map.
        auto RasterDrawsFor(std::optional<size_t> inputIndex = {}, bool allCommands = false) const
        {
            return DrawsFor(inputIndex) | std::views::filter([allCommands](const Draw& draw) {
                return allCommands || draw.bucketRepresentative;
            });
        }
        std::shared_ptr<const GpuGeometryVisibility::Frame> visibility;
        std::shared_ptr<const GpuGeometryVisibility::Frame> cameraMeshlets;
        std::shared_ptr<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
        std::shared_ptr<const IndirectMaterialBatch> materialCommands, depthCommands, temporalCommands;
        std::shared_ptr<const SceneMaterialWork> materialWork;
        std::shared_ptr<const IndirectMaterialBatch> indexedCommands;
        mutable std::atomic<uint32_t> indexedBucketSubmissions{}, indexedLogicalCommands{};
        mutable std::atomic<uint32_t> temporalBucketSubmissions{};
        mutable std::atomic<uint32_t> blendedBucketSubmissions{};
        std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> shadowCommands;
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> nextCameraHistory;

        RHIBindingTable emptyVisibility;
        RHITextureHandle environment;
        bool shadow{};
        mutable std::atomic<uint32_t> shadowDrawCount{};
        mutable std::array<std::atomic<uint32_t>, 3> shadowMeshletDrawCounts{}, shadowBucketSubmissions{};
        // One writer: METEOR.Scene.GBuffer recording. Consumers read after the
        // graph's recording join and native submission, never while recording.
        mutable std::vector<SceneGeometryRouteAudit::Draw> recordedGeometryRoutes;
        mutable bool geometryRouteAuditEnabled{};
        std::shared_ptr<const SceneLookupFrame> lookup, alphaLookup;
        std::shared_ptr<const SceneRuntimeEffectsFrame> runtimeEffects;
        mutable bool alphaInputsDeclared{};
        mutable std::set<std::size_t> alphaDeclared;
        std::shared_ptr<const SceneSubsurfaceFrame> subsurface;
        std::shared_ptr<const SceneRefractionFrame> refraction;
        std::shared_ptr<const SceneVolumeFrame> volume;
        std::vector<SceneVolumeBinding> volumeBindings;
        RHIBufferSlice emptyVolumeConstants, emptyVolumeBuffer;
        RHIBindingTable emptyVolumeBindings;
        RHIBindingTable emptyRuntimeInputs, emptyRuntimeBackground;
        mutable const EnhancedRenderGraph* graph{};
        mutable std::uint64_t graphEpoch{};
        mutable SceneSurfaceTargets::Outputs gbuffer;
        mutable SceneSurfaceTargets::Outputs forwardGbuffer;
        mutable bool colorDeclared{};
        mutable bool volumeDeclared{};
        mutable bool gbufferDeclared{}, shadowDeclared{}, cullDeclared{};
        mutable bool decalDeclared{};
        mutable std::array<RGHandle, 3> decalBaseline;
        std::array<RHIBufferSlice, 2> decalConstants;
        struct RuntimeBindings
        {
            RHIRenderTargetBinding color;
            RHIBindingTable common, decal, volume;
        };
        mutable std::mutex runtimeBindingMutex;
        mutable std::map<std::array<std::uint64_t, 7>, RuntimeBindings> runtimeBindings;
        mutable std::mutex forwardTargetMutex;
        mutable RHIRenderTargetBinding forwardTargets;

        RuntimeBindings RuntimeTables(const EnhancedRenderGraph::ExecuteContext& execution,
                                      const SceneSurfaceTargets::Outputs& inputs, RGHandle lighting,
                                      RGHandle ambientOcclusion, RGHandle shadowMap, bool blended,
                                      bool transmissionStage) const
        {
            const auto color = execution.ResolveHandle(lighting);
            const auto depth = execution.ResolveHandle(inputs.depth);
            const auto owners = execution.ResolveHandle(inputs.bitmask);
            const auto ao = blended ? RHITextureHandle{} : execution.ResolveHandle(ambientOcclusion);
            const auto shadowTexture = shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{};
            const std::array<std::uint64_t, 7> key{color.id, depth.id, owners.id, ao.id, shadowTexture.id,
                                                   transmissionStage || blended, blended && bool(volume)};
            std::lock_guard lock(runtimeBindingMutex);
            auto& result = runtimeBindings[key];
            if (!result.color.IsValid())
            {
                const auto depthView = RHIDepthTargetDesc::DepthReadOnly(depth, RHIFormat::D32Float);
                result.color = device->CreateRenderTargets({&color, 1}, &depthView);
                const RHIBindingDesc descriptions[]{
                    RHIBindingDesc::SrvCube(environment, environment.IsValid() ? device->DescribeTexture(environment).format
                                                                            : RHIFormat::RGBA16Float, 1).OrNull(),
                    RHIBindingDesc::Srv2D(owners, RHIFormat::R32Uint),
                    RHIBindingDesc::Srv2D(ao, RHIFormat::RG16Float).OrNull(),
                    RHIBindingDesc::SrvArray(shadowTexture, RHIFormat::R32Float, 3).OrNull()};
                result.common = device->CreateBindings(descriptions);
                result.decal = DecalTable(execution, transmissionStage || blended);
                if (volume && blended)
                {
                    result.volume = device->CreateBindings(volume->LightingBindings(shadowTexture));
                }
            }
            if (!result.color.IsValid() || !result.common.IsValid() || !result.decal.IsValid() ||
                (volume && blended && !result.volume.IsValid()))
            {
                throw std::runtime_error("Runtime special immutable stream bindings could not be allocated.");
            }
            return result;
        }

        void AddDecalUses(std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool transmissionStage,
                          RGAccessMode readAccess = RGAccessMode::LegacyState) const
        {
            if (!decalDeclared || transmissionStage)
            {
                return;
            }
            for (const auto handle : {gbuffer.diffuse, gbuffer.metalRough, gbuffer.normal})
            {
                uses.push_back({handle, RHIResourceState::PixelShaderResource, readAccess});
            }
            for (const auto handle : decalBaseline)
            {
                uses.push_back({handle, RHIResourceState::PixelShaderResource, readAccess});
            }
        }

        RHIBindingTable DecalTable(const EnhancedRenderGraph::ExecuteContext& execution, bool transmissionStage) const
        {
            const bool enabled = decalDeclared && !transmissionStage;
            const std::array<RGHandle, 6> handles{gbuffer.diffuse,  gbuffer.metalRough, gbuffer.normal,
                                                  decalBaseline[0], decalBaseline[1],   decalBaseline[2]};
            std::array<RHIBindingDesc, 6> descriptions;
            for (unsigned i = 0; i < handles.size(); ++i)
            {
                descriptions[i] =
                    RHIBindingDesc::Srv2D(enabled ? execution.ResolveHandle(handles[i]) : RHITextureHandle{},
                                          RHIFormat::RGBA16Float)
                        .OrNull();
            }
            const auto table = device->CreateBindings(descriptions);
            if (!table.IsValid())
            {
                throw std::runtime_error("METEOR Scene Decal binding failed.");
            }
            return table;
        }

        void BindDecal(RHIEncoder& encoder, const Draw& draw, RHIBindingTable table, bool transmissionStage) const
        {
            const unsigned index = 4 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0);
            encoder.SetBindings(RHIBindPoint::Graphics, index, table);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, index + 1,
                                      decalConstants[decalDeclared && !transmissionStage]);
        }

        void BindForward(RHIEncoder& encoder, const Draw& draw, const EnhancedForwardLighting& forward) const
        {
            const unsigned index = 6 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index,
                                  forward.lights.IsValid() ? forward.lights : draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 1,
                                  forward.counts.IsValid() ? RHIBufferSlice::Whole(forward.counts) : draw.constants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 2,
                                  forward.indices.IsValid() ? RHIBufferSlice::Whole(forward.indices) : draw.constants);
            const bool medium = draw.blended && bool(volume);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, index + 3,
                                      medium ? volume->TransportConstants() : emptyVolumeConstants);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 4, medium ? volume->Triangles() : emptyVolumeBuffer);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 5,
                                  medium ? RHIBufferSlice::Whole(volume->Coefficients()) : emptyVolumeBuffer);
            const auto table =
                medium && forward.volumeTable.IsValid() ? forward.volumeTable : emptyVolumeBindings;
            if (!table.IsValid())
            {
                throw std::runtime_error("Forward medium binding failed.");
            }
            encoder.SetBindings(RHIBindPoint::Graphics, index + 6, table);
            const auto& sceneLookup = draw.blended ? alphaLookup : lookup;
            sceneLookup->BindRuntime(encoder, index + 7);
            if (draw.program->hasSpecial && !runtimeEffects)
            {
                // Offline 분기는 tile을 읽지 않지만 공유 PSO의 root는 모두
                // 유효하게 둔다. 전체 화면 bake 레코드 계약은 그대로 유지한다.
                const unsigned effectIndex = index + 10;
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, effectIndex, emptyVolumeConstants);
                encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 1, emptyRuntimeInputs);
                encoder.SetBindings(RHIBindPoint::Graphics, effectIndex + 2, emptyRuntimeBackground);
            }
        }

        bool UsesMeshlets(const Draw& draw) const
        {
            return (draw.program->hasSurface || draw.program->hasSpecial) &&
                draw.program->meshEnabled && !draw.blended && draw.geometry->MeshletCount() > 0 &&
                device->GetMeshShaderCapabilities().SupportsDispatch(draw.geometry->MeshletCount(), 1, 1);
        }

        RHIPipelineHandle RasterPipeline(const Draw& draw, RHIPipelineHandle indexed) const
        {
            if (!UsesMeshlets(draw))
            {
                return indexed;
            }
            const auto found = draw.program->meshPipelines.find(indexed.id);
            if (found == draw.program->meshPipelines.end())
            {
                throw std::runtime_error("METEOR Scene meshlet pipeline was not prepared.");
            }
            return found->second;
        }

        bool UsesVisibility(const Draw& draw) const
        {
            if (!visibility || draw.visibilityBin == UINT32_MAX)
                throw std::runtime_error("METEOR Scene raster requires its GPU visibility bin.");
            return true;
        }

        void AddVisibilityReads(EnhancedRenderGraph& graph,
                                std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
        {
            if (visibility)
            {
                visibility->AddVisibleIdsRead(graph, uses);
            }

        }

        void AddGeometryReads(EnhancedRenderGraph& graph,
                              std::vector<EnhancedRenderGraph::RGPassUsage>& uses, bool blended) const
        {
            if (blended) {
                AddVisibilityReads(graph, uses);
                if (indexedCommands) indexedCommands->AddRead(graph, uses);
            } else {
                if (cameraMeshlets) cameraMeshlets->AddVisibleIdsRead(graph, uses);
                if (materialCommands) materialCommands->AddRead(graph, uses);
            }
        }

        void AddReconstructionReads(EnhancedRenderGraph& graph,
            std::vector<EnhancedRenderGraph::RGPassUsage>& uses, RGHandle visibilityTarget) const
        {
            const auto read = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read;
            uses.push_back({visibilityTarget, RHIResourceState::PixelShaderResource, read});
            uses.push_back({geometryTable->GraphTable(graph), RHIResourceState::PixelShaderResource, read});
            if (materialWork) materialWork->AddRead(graph, uses);
        }
        void BindReconstruction(const EnhancedRenderGraph::ExecuteContext& execution, const Draw& draw,
            RGHandle visibilityTarget, RHIPipelineHandle source) const
        {
            auto& encoder = *execution.encoder;
            encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->reconstructionPipelines.at(source.id));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->geometryTableRoot, RHIBufferSlice::Whole(geometryTable->buffer));
            const auto desc = RHIBindingDesc::Srv2D(execution.ResolveHandle(visibilityTarget), RHIFormat::RG32Uint);
            const auto table = device->CreateBindings({&desc, 1});
            if (!table.IsValid()) throw std::runtime_error("METEOR reconstruction visibility binding failed.");
            encoder.SetBindings(RHIBindPoint::Graphics, draw.program->visibilityRoot, table);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->materialWorkRoot, RHIBufferSlice::Whole(materialWork->work));
        }
        void DrawReconstruction(RHIEncoder& encoder, const Draw& draw) const
        {
            if (!materialWork) throw std::runtime_error("METEOR material tile producer is missing.");
            materialWork->Draw(encoder, draw.materialTileBucket);
        }

        void BindGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            encoder.SetBindings(RHIBindPoint::Graphics, draw.program->visibilityRoot, emptyVisibility);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->geometryTableRoot, RHIBufferSlice::Whole(geometryTable->buffer));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            // Runtime IBL owns the preceding two slots; material bindings follow
            // this final host slot and are resolved by the reflected PassLayout.
            const unsigned index = 15 + (draw.program->hasSpecial ? 2 : 0) +
                                   (draw.program->hasTransmission ? 1 : 0);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index,
                visibility->VisibleIds(draw.visibleIdOffset, 1));
            if (UsesMeshlets(draw))
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->temporalPreviousRoot + 1,
                    cameraMeshlets->VisibleIds(cameraMeshlets->MeshletOutputOffset(draw.cameraMeshletBin),
                        draw.geometry->MeshletCount() + 1));
        }

        void DrawGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            if (UsesMeshlets(draw))
            {
                if (!materialCommands || draw.materialBucket == UINT32_MAX)
                    throw std::runtime_error("LX surface has no material command bucket.");
                materialCommands->Draw(encoder, draw.materialBucket);
                static std::atomic<bool> reported{};
                if (!reported.exchange(true))
                {
                    std::printf("[meteor.meshlets] surface dispatch groups=%u indirect=%u\n", draw.geometry->MeshletCount(), unsigned(UsesVisibility(draw)));
                }
                return;
            }
            if (!indexedCommands || draw.indexedBucket == UINT32_MAX)
                throw std::runtime_error("METEOR layer has no indexed command bucket; no individual submission fallback.");
            indexedCommands->Draw(encoder, draw.indexedBucket);
            indexedBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
            indexedLogicalCommands.fetch_add(indexedCommands->buckets[draw.indexedBucket].count, std::memory_order_relaxed);
        }

        struct BlendedRange {
            std::vector<size_t> inputs;
            std::array<std::shared_ptr<const IndirectMaterialBatch>, 2> commands;
        };
        std::vector<BlendedRange> blendedRanges;
        RHIBufferSlice emptyMediumConstants, emptyMediumTriangles;
        bool PrepareBlended(const std::shared_ptr<const LX::Runtime::ComputeGeneration>& generation,
                            std::string& error)
        {
            // Back to front. Equal depths keep a stable geometry/draw order.
            std::vector<ForwardDraw> order;
            for (size_t i = 0; i < input->Draws().size(); ++i) {
                const auto& draw = input->Draws()[i];
                const auto& p = draw.material->generation->cooked.product.program;
                if (p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u)))
                    order.push_back({i, draw.geometryKey, draw.viewDepth});
            }
            for (const auto& draw : order) if (!std::isfinite(draw.depth))
                throw std::runtime_error("METEOR blended sorting depth is non-finite.");
            std::stable_sort(order.begin(), order.end(), [](const ForwardDraw& a, const ForwardDraw& b) {
                if (a.depth != b.depth) return a.depth > b.depth;
                if (a.geometryKey != b.geometryKey) return a.geometryKey < b.geometryKey;
                return a.index < b.index;
            });
            const auto mergePipeline = [&](std::size_t input) {
                RHIPipelineHandle result{};
                if (alphaLookup->RequiresCapture()) return result;
                for (const auto& draw : DrawsFor(input)) {
                    // Surface effects consume per-layer captures/backgrounds. Those
                    // are semantic ordering boundaries, not alternative alpha paths.
                    if (!draw.program->hasSurface || draw.program->hasSpecial || draw.program->hasTransmission) return RHIPipelineHandle{};
                    const auto pipeline = draw.program->blendedColor[draw.doubleSided].GetHandle();
                    if (result.IsValid() && result != pipeline) return RHIPipelineHandle{};
                    result = pipeline;
                }
                return result;
            };
            for (std::size_t first = 0; first < order.size();)
            {
                const auto pipeline = mergePipeline(order[first].index);
                std::vector<std::size_t> group{order[first++].index};
                while (first < order.size() && pipeline.IsValid() && mergePipeline(order[first].index) == pipeline)
                    group.push_back(order[first++].index);
                blendedRanges.push_back({std::move(group), {}});
            }

            if (!blendedRanges.empty() && !volume) {
                const std::array<std::byte, SceneVolumeConstantsBytes> empty{};
                emptyMediumConstants = device->UploadConstants(empty.data(), empty.size());
                emptyMediumTriangles = device->AllocateUpload({48, RHIUploadUsage::Raw, 16});
                if (!emptyMediumConstants.IsValid() || !emptyMediumTriangles.IsWritable())
                    return Fail(error, "METEOR blended empty medium allocation failed.");
                std::memset(emptyMediumTriangles.cpuAddress, 0, 48);
            }
            for (auto& range : blendedRanges) {
                const bool transmissionStage = (input->Draws()[range.inputs.front()].material->generation->cooked.product.program.features & 0x0800u) != 0;
                for (unsigned reference = 0; reference != 2; ++reference) {
                auto blendedCommands = std::make_shared<IndirectMaterialBatch>();
                blendedCommands->name = "METEOR.Scene.Commands.Blended." + std::to_string(range.inputs.front()) + (reference ? ".Reference" : ".Color");
                blendedCommands->device = device;
                blendedCommands->pipeline = generation;
                blendedCommands->visibility = visibility;
                blendedCommands->indexedReplay = blendedCommands->indexedVisibility = true;
                std::vector<uint32_t> words;
                std::vector<std::array<uint32_t, 4>> records;
                const auto& order = range.inputs;
                RHIPipelineHandle pipeline{};

                for (const auto input : order) for (const auto& draw : DrawsFor(input))
                {
                    if (!draw.program->hasSurface || draw.program->hasTransmission != transmissionStage ||
                        (runtimeEffects && draw.program->hasSpecial)) continue;
                    const auto current = draw.program->blendedColor[draw.doubleSided].GetHandle();
                    if (blendedCommands->buckets.empty()) {
                        IndirectMaterialBatch::Bucket bucket;
                        bucket.representative = &draw - draws.data();
                        blendedCommands->buckets.push_back(std::move(bucket));
                        pipeline = current;
                    }
                    if (pipeline != current) throw std::runtime_error("METEOR blended range crossed a PSO boundary.");
                    auto& bucket = blendedCommands->buckets.front();
                    const uint32_t first = static_cast<uint32_t>(words.size() * 4);
                    std::vector<uint32_t> roots;
                    const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                        uint64_t address{};
                        if (!device->EncodeIndirectBuffer(slice, address) || (cbv && address % 256))
                            throw std::runtime_error("METEOR blended indirect root preparation failed.");
                        roots.push_back(slot | (cbv ? 0x80000000u : 0));
                        words.push_back(uint32_t(address)); words.push_back(uint32_t(address >> 32));
                    };
                    append(0, reference ? draw.referenceConstants : draw.constants, true);
                    append(1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    append(15 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0),
                        visibility->VisibleIds(draw.visibleIdOffset, 1));
                    const auto& layout = draw.program->layout;
                    if (layout.uniformSlot) append(*layout.uniformSlot, draw.bindings->uniforms, true);
                    if (layout.descriptorIndexSlot) append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true);
                    if (bucket.roots.empty()) bucket.roots = roots;
                    if (bucket.roots != roots) throw std::runtime_error("METEOR blended indirect root ABI mismatch.");
                    std::array<uint32_t, 4> indexView{};
                    if (!device->EncodeIndirectIndexBuffer(draw.indices, RHIFormat::R32Uint, indexView))
                        throw std::runtime_error("METEOR blended indirect index view unavailable.");
                    words.insert(words.end(), indexView.begin(), indexView.end());
                    words.insert(words.end(), 5, 0); // GPU visibility fills all indexed draw arguments.
                    records.push_back({first, first, uint32_t(roots.size() * 2 + 9), static_cast<uint32_t>(visibility->ArgsOffset(draw.visibilityBin))});
                    ++bucket.count;
                }
                if (!blendedCommands->Upload(words, records, uint32_t(words.size() * 4), error))
                    throw std::runtime_error(error);

                range.commands[reference] = std::move(blendedCommands);
                }
            }
            return true;
        }

        void CheckCurrent(const EnhancedRenderGraph* currentGraph = nullptr) const
        {
            if (device->GetCurrentUploadRecordingId() != recording ||
                device->GetDescriptorVersionToken() != descriptors)
            {
                throw std::runtime_error("METEOR Scene frame has stale upload/descriptor ownership.");
            }
            if (currentGraph && (graph != currentGraph || currentGraph->ResourceEpoch() != graphEpoch))
            {
                throw std::runtime_error("METEOR Scene frame belongs to a reset graph.");
            }
        }
    };

    struct SceneHost::Preparation
    {
        struct Work : SceneShaderSet
        {
            own::shared_owner<const Generation> generation;
            own::unique_owner<const VerifiedProduct> verified;
            RHIShaderBinary backend{};
            std::filesystem::path file, shaderDirectory;
            std::string error;
            bool worker{}, cooked{};
        };
        own::shared_owner<const Generation> generation;
        RHIShaderBinary backend{};
        own::shared_owner<Work> work;
        job_handle job;
        own::shared_owner<Program> program;
        std::string error;
        bool checked{};
    };

    struct SceneHost::Slot
    {
        own::shared_owner<const Instance> requested, active;
        EnhancedMaterialCoverage requestedCoverage, activeCoverage;
        std::uint64_t revision{};
    };

    SceneHost::SceneHost(job_scheduler& scheduler) : scheduler_(scheduler)
    {
    }

    SceneHost::~SceneHost()
    {
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
    }

    bool SceneHost::IsProgramReady(const own::shared_owner<const Generation>& generation, RHIShaderBinary backend) const
    {
        return std::ranges::any_of(programs_, [&](const auto& program) {
            return SamePinnedObject(program->generation, generation) && program->backend == backend;
        });
    }

    SceneProgramStats SceneHost::ProgramStats() const
    {
        auto result = stats_;
        result.ready = programs_.size();
        result.pending = std::ranges::count_if(preparations_, [](const auto& item) { return item->error.empty(); });
        result.activeSlots = std::ranges::count_if(slots_, [](const auto& item) { return bool(item.second->active); });
        std::lock_guard lock(recordingMutex_);
        result.retainedRecordings = recordings_.size();
        return result;
    }

    void SceneHost::PruneFailedPreparations(const own::shared_owner<const Generation>& requested)
    {
        std::size_t retainedFailures = std::ranges::count_if(preparations_, [](const auto& item)
        {
            return !item->error.empty();
        });
        std::erase_if(preparations_, [&](const auto& preparation)
        {
            if (retainedFailures <= 16 || preparation->error.empty()
                || SamePinnedObject(preparation->generation, requested)
                || (preparation->job.valid() && !preparation->job.is_complete()))
            {
                return false;
            }
            const bool referenced = std::ranges::any_of(slots_, [&](const auto& item)
            {
                return (item.second->requested
                        && SamePinnedObject(item.second->requested->generation, preparation->generation))
                    || (item.second->active
                        && SamePinnedObject(item.second->active->generation, preparation->generation));
            });
            if (referenced)
            {
                return false;
            }
            --retainedFailures;
            return true;
        });
    }

    bool SceneHost::RequestProgram(const EnhancedFrameContext& context, own::shared_owner<const Generation> generation,
                                   std::string& error)
    {
        const auto backend = RHIShaderCompiler::GetOutput();
        if (!generation || !context.resources || !context.rootSignatures || !context.psoManager ||
            (programDevice_ && programDevice_ != context.resources))
        {
            return Fail(error, "METEOR Scene program preparation needs its owning device and immutable generation.");
        }
        programDevice_ = context.resources;
        if (IsProgramReady(generation, backend))
        {
            error.clear();
            return true;
        }
        for (const auto& item : preparations_)
        {
            if (SamePinnedObject(item->generation, generation) && item->backend == backend)
            {
                error = item->error;
                return error.empty();
            }
        }
        PruneFailedPreparations(generation);
        if (preparations_.size() >= 64)
        {
            PollPrograms(context);
            PruneFailedPreparations(generation);
            if (preparations_.size() >= 64)
            {
                return Fail(error, PreparationAdmissionError);
            }
        }
        auto preparation = own::make_unique<Preparation>();
        preparation->generation = std::move(generation);
        preparation->backend = backend;
        const auto reject = [&](std::string message) {
            preparation->error = message;
            stats_.lastError = message;
            ++stats_.failedPreparations;
            preparations_.push_back(std::move(preparation));
            return Fail(error, std::move(message));
        };
        const auto& product = preparation->generation->cooked.product;
        Capabilities capabilities;
        capabilities.coreForward = true;
        capabilities.layeredLookup = true;
        capabilities.subsurface = true;
        capabilities.refraction = true;
        capabilities.volume = true;
        Selection selection;
        std::vector<LX::LXMaterialDiagnostic> diagnostics;
        if (!SelectRoute(product.program, capabilities, {}, selection, diagnostics))
        {
            return reject(diagnostics.empty() ? "METEOR Scene route rejected." : diagnostics.front().message);
        }
        if (product.program.volume &&
            product.program.slang.find("#define LX_MATERIAL_VOLUME_HOMOGENEOUS 1\n") == std::string::npos)
        {
            return reject(
                "METEOR Scene Volume requires homogeneous coefficients; spatially varying Volume is unsupported.");
        }
        if (!product.program.semanticKey.ends_with(SceneHostIdentity))
            return reject("METEOR Scene ABI 22 requires force reimport/recook; runtime conversion is forbidden.");
        auto work = own::make_shared<Preparation::Work>();
        work->generation = preparation->generation;
        work->backend = backend;
        if (!LoadSceneShaders(product, backend, *work, error)) return reject(error);
        work->cooked = true;
        preparation->work = std::move(work);
        preparations_.push_back(std::move(preparation));
        error.clear();
        return true;
    }

    void SceneHost::PollPrograms(const EnhancedFrameContext& context)
    {
        if (programDevice_ != context.resources || !context.rootSignatures || !context.psoManager)
        {
            return;
        }
        PollSubmittedFrames();
        // Start native requests for several materials in one frame. RequestGraphics
        // queues the DX12 work asynchronously; one request per frame made a model's
        // many material slots appear over a long series of frames.
        unsigned pollsRemaining = 8;
        for (const auto& preparation : preparations_)
        {
            const bool cooked = preparation->work && preparation->work->cooked;
            const bool prepared = preparation->checked && preparation->program;
            if (!preparation->error.empty() ||
                (!prepared && !cooked && (!preparation->job.valid() || !preparation->job.is_complete())))
            {
                continue;
            }
            const auto reject = [&](std::string message) {
                preparation->error = std::move(message);
                preparation->program.reset();
                preparation->work.reset();
                stats_.lastError = preparation->error;
                ++stats_.failedPreparations;
            };
            if (!preparation->checked)
            {
                try
                {
                    if (preparation->job.valid())
                    {
                        preparation->job.wait();
                    }
                }
                catch (const std::exception& exception)
                {
                    reject(exception.what());
                    continue;
                }
                preparation->checked = true;
                const auto& compiled = preparation->work;
                if (compiled->worker)
                {
                    ++stats_.workerExecutions;
                }
                if (!compiled->error.empty())
                {
                    reject(compiled->error);
                    continue;
                }
                const auto& vertex = compiled->vertex;
                const auto& gbuffer = compiled->gbuffer;
                const auto& color = compiled->color;
                const auto& lookup0 = compiled->lookup0;
                const auto& lookup1 = compiled->lookup1;
                const auto& layout = compiled->layout;
                const auto backend = preparation->backend;
                std::string error;
                const auto describe = [&]() -> bool {
                    auto candidate = own::make_shared<Program>();
                    candidate->generation = preparation->generation;
                    candidate->backend = backend;
                    const auto caps = context.resources->GetMeshShaderCapabilities();
                    candidate->meshEnabled = caps.meshShader && caps.meshIndirect &&
                        caps.maxOutputVertices >= 64 && caps.maxOutputPrimitives >= 126 &&
                        caps.maxThreadsPerGroup >= 64 && caps.maxThreadGroupSizeX >= 64 &&
                        caps.maxOutputMemoryBytes >= 8192;
                    candidate->meshShader = compiled->mesh.bytecode;
                    candidate->visibilityMeshShader = compiled->visibilityMesh.bytecode;
                    candidate->reconstructShader = compiled->reconstructMesh.bytecode;
                    candidate->shadowMeshShader = compiled->shadowMesh.bytecode;
                    const auto& product =
                        compiled->verified ? *compiled->verified : candidate->generation->cooked.product;
                    const auto prepare = [&](LX::Runtime::GraphicsPipeline& request,
                                             const RHIGraphicsPipelineDesc& desc, std::string_view vs,
                                             std::string_view ps) {
                        LX::Runtime::GraphicsShaderDescription shader;
                        return DescribeGraphicsShader(product, backend, vs, ps, shader, error) &&
                               request.Prepare(desc, std::move(shader), error);
                    };
                    candidate->hasSurface = candidate->generation->cooked.product.program.surface;
                    candidate->hasVolume = candidate->generation->cooked.product.program.volume;
                    candidate->hasSubsurface = (candidate->generation->cooked.product.program.features & 0x1000u) != 0;
                    candidate->hasTransmission =
                        (candidate->generation->cooked.product.program.features & 0x0800u) != 0;
                    candidate->hasSpecial = candidate->hasSubsurface || candidate->hasTransmission;
                    std::vector<RHIPipelineLayoutParam> host{RHILayout::Cbv(0),
                                                             RHILayout::Srv(0, RHIShaderVisibility::All),
                                                             RHILayout::SrvTable(4, 1, RHIShaderVisibility::Pixel),
                                                             RHILayout::Srv(5, RHIShaderVisibility::Pixel)};
                    if (candidate->hasSpecial)
                    {
                        host.push_back(RHILayout::Srv(6, RHIShaderVisibility::Pixel));
                        host.push_back(RHILayout::Srv(7, RHIShaderVisibility::Pixel));
                    }
                    if (candidate->hasTransmission)
                    {
                        host.push_back(RHILayout::Srv(8, RHIShaderVisibility::Pixel));
                    }
                    host.push_back(RHILayout::SrvTable(6, 9, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(3, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(128, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(129, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(130, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(5, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(131, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(132, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::SrvTable(2, 133, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::SrvTable(3, 135, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(6, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(138, RHIShaderVisibility::All));
                    if (candidate->hasSpecial)
                    {
                        host.push_back(RHILayout::Cbv(7, RHIShaderVisibility::Pixel));
                        host.push_back(RHILayout::SrvTable(14, 139, RHIShaderVisibility::Pixel));
                        host.push_back(RHILayout::SrvTable(2, 153, RHIShaderVisibility::Pixel));
                    }
                    candidate->meshletRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(155));
                    candidate->temporalPreviousRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(156, RHIShaderVisibility::All));
                    host.push_back(RHILayout::Srv(157));
                    candidate->geometryTableRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(158, RHIShaderVisibility::Pixel));
                    candidate->visibilityRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::SrvTable(1, 159, RHIShaderVisibility::Pixel));
                    candidate->materialWorkRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(160));
                    candidate->rasterRouteRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(161));
                    candidate->visibilityKeyRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Uav(1, RHIShaderVisibility::Pixel));
                    const RHIStaticSamplerDesc samplers[]{
                        {RHISampler::Linear(RHIAddressMode::Clamp), 0, RHIShaderVisibility::Pixel},
                        {RHISampler::Comparison(RHICompareOp::LessEqual, RHIAddressMode::Border,
                                                RHIBorderColor::OpaqueWhite),
                         1, RHIShaderVisibility::Pixel},
                        {RHISampler::Point(RHIAddressMode::Clamp), 2, RHIShaderVisibility::Pixel}};
                    if (!CreatePassLayout(*context.rootSignatures, layout, host, samplers, false, candidate->layout,
                                          error))
                    {
                        return false;
                    }
                    if (candidate->hasSurface)
                    {
                        const RHIPipelineLayoutParam shadowHost[]{RHILayout::Cbv(4),
                                                                  RHILayout::Srv(0, RHIShaderVisibility::All), RHILayout::Srv(155), RHILayout::Srv(157)};
                        if (!CreatePassLayout(*context.rootSignatures, layout, shadowHost, {}, false,
                                              candidate->shadowLayout, error))
                        {
                            return false;
                        }
                        RHIGraphicsPipelineDesc desc;
                        desc.layout = candidate->shadowLayout.handle;
                        desc.vsBytecode = compiled->shadowVertex.bytecode.Data();
                        desc.vsSize = compiled->shadowVertex.bytecode.Size();
                        desc.psBytecode = compiled->shadow.bytecode.Data();
                        desc.psSize = compiled->shadow.bytecode.Size();
                        desc.depthEnable = true;
                        desc.depthWriteMask = RHIDepthWrite::All;
                        desc.depthFunc = RHICompareOp::Less;
                        desc.cullMode = RHICullMode::None;
                        desc.dsvFormat = RHIFormat::D32Float;
                        desc.numRenderTargets = 0;
                        if (!prepare(candidate->shadow, desc, "LXSceneShadowVS", "LXSceneShadowPS"))
                        {
                            return false;
                        }
                    }
                    if (candidate->hasVolume)
                    {
                        const RHIPipelineLayoutParam volumeHost[]{RHILayout::Cbv(1), RHILayout::UavBufferTable(1, 0)};
                        if (!CreatePassLayout(*context.rootSignatures, layout, volumeHost, {}, false,
                                              candidate->volumeLayout, error))
                        {
                            return false;
                        }
                        RHIComputePipelineDesc desc;
                        desc.layout = candidate->volumeLayout.handle;
                        desc.csBytecode = compiled->volume.bytecode.Data();
                        desc.csSize = compiled->volume.bytecode.Size();
                        LX::Runtime::ComputeShaderDescription shader;
                        if (!DescribeComputeShader(product, backend, "LXSceneVolumeCoefficientCS", shader, error) ||
                            !candidate->volume.Create(*context.psoManager, desc, std::move(shader), error))
                        {
                            return false;
                        }
                    }
                    for (unsigned side = 0; side < 2; ++side)
                    {
                        RHIGraphicsPipelineDesc desc;
                        desc.layout = candidate->layout.handle;
                        desc.vsBytecode = vertex.bytecode.Data();
                        desc.vsSize = vertex.bytecode.Size();
                        desc.psBytecode = gbuffer.bytecode.Data();
                        desc.psSize = gbuffer.bytecode.Size();
                        desc.depthEnable = true;
                        desc.depthWriteMask = RHIDepthWrite::All;
                        desc.depthFunc = RHICompareOp::Less;
                        desc.cullMode = side ? RHICullMode::None : RHICullMode::Back;
                        desc.dsvFormat = RHIFormat::D32Float;
                        desc.numRenderTargets = 5;
                        for (unsigned i = 0; i < 5; ++i)
                        {
                            desc.rtvFormats[i] = SceneSurfaceTargets::GetRenderTargetFormat(i);
                        }
                        if (!prepare(candidate->gbuffer[side], desc, "LXSceneVS", "LXSceneGBufferPS"))
                        {
                            return false;
                        }
                        auto visibilityDesc = desc;
                        visibilityDesc.psBytecode = compiled->visibility.bytecode.Data();
                        visibilityDesc.psSize = compiled->visibility.bytecode.Size();
                        visibilityDesc.numRenderTargets = 0;
                        visibilityDesc.depthEnable = false;
                        visibilityDesc.depthWriteMask = RHIDepthWrite::Zero;
                        visibilityDesc.dsvFormat = RHIFormat::Unknown;
                        std::fill(std::begin(visibilityDesc.rtvFormats), std::end(visibilityDesc.rtvFormats), RHIFormat::Unknown);
                        if (!prepare(candidate->visibilitySurface[side], visibilityDesc, "LXSceneVS", "LXSceneVisibilityPS")) return false;
                        auto temporalDesc = desc;
                        temporalDesc.vsBytecode = compiled->temporalVertex.bytecode.Data();
                        temporalDesc.vsSize = compiled->temporalVertex.bytecode.Size();
                        temporalDesc.psBytecode = compiled->temporal.bytecode.Data();
                        temporalDesc.psSize = compiled->temporal.bytecode.Size();
                        temporalDesc.numRenderTargets = 4;
                        temporalDesc.depthFunc = RHICompareOp::LessEqual;
                        std::fill(std::begin(temporalDesc.rtvFormats), std::end(temporalDesc.rtvFormats), RHIFormat::Unknown);
                        temporalDesc.rtvFormats[0] = RHIFormat::RG16Float;
                        std::fill_n(std::begin(temporalDesc.rtvFormats) + 1, 3, RHIFormat::R16Float);
                        if (!prepare(candidate->temporal[side], temporalDesc, "LXSceneTemporalVS", "LXSceneTemporalPS")) return false;
                        auto depthDesc = desc;
                        depthDesc.numRenderTargets = 0;
                        depthDesc.psBytecode = compiled->depth.bytecode.Data();
                        depthDesc.psSize = compiled->depth.bytecode.Size();
                        if (!prepare(candidate->depth[side], depthDesc, "LXSceneVS", "LXSceneDepthPS"))
                        {
                            return false;
                        }
                        desc.psBytecode = color.bytecode.Data();
                        desc.psSize = color.bytecode.Size();
                        desc.depthWriteMask = RHIDepthWrite::Zero;
                        desc.depthFunc = RHICompareOp::Equal;
                        desc.numRenderTargets = 1;
                        desc.rtvFormats[0] = RHIFormat::RGBA16Float;
                        std::fill(std::begin(desc.rtvFormats) + 1, std::end(desc.rtvFormats), RHIFormat::Unknown);
                        if (!prepare(candidate->color[side], desc, "LXSceneVS", "LXSceneColorPS"))
                        {
                            return false;
                        }
                        if (candidate->hasSurface)
                        {
                            desc.depthFunc = RHICompareOp::LessEqual;
                            desc.blendEnable = true;
                            if (!prepare(candidate->blendedColor[side], desc, "LXSceneVS", "LXSceneColorPS"))
                            {
                                return false;
                            }
                            desc.blendEnable = false;
                            desc.depthFunc = RHICompareOp::Equal;
                        }
                        if (candidate->hasSubsurface)
                        {
                            desc.psBytecode = compiled->subsurface.bytecode.Data();
                            desc.psSize = compiled->subsurface.bytecode.Size();
                            desc.numRenderTargets = 7;
                            std::fill_n(std::begin(desc.rtvFormats), 7, RHIFormat::RGBA32Float);
                            if (!prepare(candidate->subsurface[side], desc, "LXSceneVS", "LXSceneSubsurfacePS"))
                            {
                                return false;
                            }
                        }
                        if (candidate->hasTransmission)
                        {
                            desc.psBytecode = compiled->refraction.bytecode.Data();
                            desc.psSize = compiled->refraction.bytecode.Size();
                            desc.numRenderTargets = 2;
                            std::fill(std::begin(desc.rtvFormats), std::end(desc.rtvFormats), RHIFormat::Unknown);
                            std::fill_n(std::begin(desc.rtvFormats), 2, RHIFormat::RGBA32Float);
                            if (!prepare(candidate->refraction[side], desc, "LXSceneVS", "LXSceneRefractionPS"))
                            {
                                return false;
                            }
                        }
                        for (unsigned part = 0; part < 2; ++part)
                        {
                            const auto& shader = part ? lookup1 : lookup0;
                            desc.psBytecode = shader.bytecode.Data();
                            desc.psSize = shader.bytecode.Size();
                            desc.numRenderTargets = part ? 3 : 8;
                            std::fill(std::begin(desc.rtvFormats), std::end(desc.rtvFormats), RHIFormat::Unknown);
                            std::fill_n(std::begin(desc.rtvFormats), desc.numRenderTargets, RHIFormat::RGBA32Float);
                            if (!prepare(candidate->lookup[part][side], desc, "LXSceneVS",
                                         part ? "LXSceneLookup1PS" : "LXSceneLookup0PS"))
                            {
                                return false;
                            }
                            if (candidate->hasSurface)
                            {
                                desc.depthFunc = RHICompareOp::LessEqual;
                                if (!prepare(candidate->blendedLookup[part][side], desc, "LXSceneVS",
                                             part ? "LXSceneLookup1PS" : "LXSceneLookup0PS"))
                                {
                                    return false;
                                }
                                desc.depthFunc = RHICompareOp::Equal;
                            }
                        }
                        if (candidate->hasSpecial)
                        {
                            for (unsigned part = 0; part < 2; ++part)
                            {
                                const auto& shader = part ? compiled->runtimeEffects1 : compiled->runtimeEffects0;
                                desc.psBytecode = shader.bytecode.Data();
                                desc.psSize = shader.bytecode.Size();
                                desc.numRenderTargets = part ? 6 : 8;
                                std::fill(std::begin(desc.rtvFormats), std::end(desc.rtvFormats), RHIFormat::Unknown);
                                std::fill_n(std::begin(desc.rtvFormats), desc.numRenderTargets, RHIFormat::RGBA32Float);
                                if (!prepare(candidate->runtimeEffects[part][side], desc, "LXSceneVS",
                                             part ? "LXSceneRuntimeEffects1PS" : "LXSceneRuntimeEffects0PS"))
                                {
                                    return false;
                                }
                            }
                        }
                    }

                    preparation->program = std::move(candidate);
                    return true;
                };
                if (!describe())
                {
                    reject(error);
                    continue;
                }
                preparation->work.reset();
            }
            if (!pollsRemaining)
            {
                continue;
            }
            auto& program = *preparation->program;
            std::vector<LX::Runtime::GraphicsPipeline*> requests{
                &program.temporal[0], &program.temporal[1],
                &program.depth[0], &program.depth[1],
                &program.visibilitySurface[0], &program.visibilitySurface[1],
                &program.gbuffer[0],   &program.gbuffer[1],   &program.color[0],     &program.color[1],
                &program.lookup[0][0], &program.lookup[0][1], &program.lookup[1][0], &program.lookup[1][1]};
            if (program.hasSurface)
            {
                for (unsigned side = 0; side < 2; ++side)
                {
                    requests.push_back(&program.blendedColor[side]);
                    for (unsigned part = 0; part < 2; ++part)
                    {
                        requests.push_back(&program.blendedLookup[part][side]);
                    }
                }
            }
            if (program.hasSurface)
            {
                requests.push_back(&program.shadow);
            }
            if (program.hasSubsurface)
            {
                requests.push_back(&program.subsurface[0]);
                requests.push_back(&program.subsurface[1]);
            }
            if (program.hasTransmission)
            {
                requests.push_back(&program.refraction[0]);
                requests.push_back(&program.refraction[1]);
            }
            if (program.hasSpecial)
            {
                for (auto& part : program.runtimeEffects)
                {
                    requests.push_back(&part[0]);
                    requests.push_back(&part[1]);
                }
            }
            std::string error;
            for (auto* request : requests)
            {
                if (!request->IsValid())
                {
                    if (!pollsRemaining) break;
                    --pollsRemaining;
                    if (request->Poll(*context.psoManager, error) == RHIPipelineRequestState::Failed)
                    {
                        reject(error.empty() ? "METEOR Scene native PSO preparation failed." : error);
                        break;
                    }
                }
            }
            if (preparation->program &&
                std::ranges::all_of(requests, [](const auto* request) { return request->IsValid(); }))
            {
                if (program.meshEnabled)
                {
                    for (const auto* request : requests)
                    {
                        if (request == &program.temporal[0] || request == &program.temporal[1]) continue;
                        const auto indexed = request->GetHandle();
                        if (program.meshPipelines.contains(indexed.id))
                        {
                            continue;
                        }
                        const auto& raster = request->GetDesc();
                        const auto& shader = request == &program.shadow ? program.shadowMeshShader :
                            (request == &program.visibilitySurface[0] || request == &program.visibilitySurface[1]) ? program.visibilityMeshShader : program.meshShader;
                        RHIMeshPipelineDesc desc;
                        desc.msBytecode = shader.Data();
                        desc.msSize = shader.Size();
                        desc.psBytecode = raster.psBytecode;
                        desc.psSize = raster.psSize;
                        desc.layout = raster.layout;
                        desc.fillMode = raster.fillMode;
                        desc.cullMode = raster.cullMode;
                        desc.depthEnable = raster.depthEnable;
                        desc.blendEnable = raster.blendEnable;
                        desc.depthWriteMask = raster.depthWriteMask;
                        desc.depthFunc = raster.depthFunc;
                        desc.independentBlend = raster.independentBlend;
                        desc.numRenderTargets = raster.numRenderTargets;
                        desc.dsvFormat = raster.dsvFormat;
                        desc.sampleCount = raster.sampleCount;
                        for (uint32_t i = 0; i < 8; ++i)
                        {
                            desc.renderTargetBlend[i] = raster.renderTargetBlend[i];
                            desc.rtvFormats[i] = raster.rtvFormats[i];
                        }
                        const auto pipeline = context.psoManager->GetOrCreateMesh(desc, error);
                        if (!pipeline.IsValid())
                        {
                            reject("METEOR Scene meshlet PSO creation failed: " + error);
                            break;
                        }
                        program.meshPipelines.emplace(indexed.id, pipeline);
                        if (request != &program.shadow && request != &program.visibilitySurface[0] &&
                            request != &program.visibilitySurface[1] && request != &program.depth[0] && request != &program.depth[1]) {
                            desc.msBytecode = program.reconstructShader.Data();
                            desc.msSize = program.reconstructShader.Size();
                            desc.depthEnable = false;
                            desc.depthWriteMask = RHIDepthWrite::Zero;
                            desc.cullMode = RHICullMode::None;
                            const auto reconstructed = context.psoManager->GetOrCreateMesh(desc, error);
                            if (!reconstructed.IsValid()) { reject("METEOR reconstruction PSO failed: " + error); break; }
                            program.reconstructionPipelines.emplace(indexed.id, reconstructed);
                        }
                        // Budget synchronous mesh PSO creation across preparation ticks.
                        break;
                    }
                    if (!preparation->program || !std::ranges::all_of(requests, [&](const auto* request)
                        { return request == &program.temporal[0] || request == &program.temporal[1] || program.meshPipelines.contains(request->GetHandle().id); }))
                    {
                        continue;
                    }
                }
                // Allocate before the converting move. If growth fails, the
                // checked preparation still owns its ready Program for a retry.
                if (programs_.size() == programs_.capacity())
                {
                    programs_.reserve((std::max)(programs_.size() + 1, programs_.capacity() * 2));
                }
                programs_.push_back(std::move(preparation->program));
                if (!PathFinder::IsAssetAuthoringEnabled())
                {
                    std::printf("[meteor.scene.program] source=cooked graph=%s ready=%zu sceneCompiles=%llu\n",
                                Uuid::ToString(preparation->generation->assetId.value).c_str(), programs_.size(),
                                static_cast<unsigned long long>(stats_.compileSubmissions));
                    std::fflush(stdout);
                }
                preparation->program.reset();
            }
        }
        std::erase_if(preparations_, [&](const auto& item) { return IsProgramReady(item->generation, item->backend); });
    }

    bool SceneHost::PrepareProgram(const EnhancedFrameContext&, const Instance& instance,
                                   own::shared_owner<const Program>& result, std::string& error)
    {
        const auto backend = RHIShaderCompiler::GetOutput();
        for (const auto& program : programs_)
        {
            if (SamePinnedObject(program->generation, instance.generation) && program->backend == backend)
            {
                result = program;
                error.clear();
                return true;
            }
        }
        return Fail(error, "METEOR Scene program is not ready; request and poll before recording the frame.");
    }

    bool SceneHost::SelectReadyInput(const EnhancedFrameContext& context,
                                     own::shared_owner<const SceneViewInput> requested,
                                     own::shared_owner<const SceneViewInput>& result, std::string& error)
    {
        selectionDeferred_ = false;
        if (!requested)
        {
            result = std::move(requested);
            error.clear();
            return true;
        }
        const auto& view = requested->View();
        if (view.frameId != context.frameId || view.sceneEpoch != context.sceneEpoch || view.width != context.width ||
            view.height != context.height)
        {
            return Fail(error, "METEOR Scene selection needs the current identified view.");
        }
        if (requested->Draws().empty())
        {
            std::erase_if(slots_, [&](const auto& item) { return std::get<1>(item.first) == view.viewId; });
            PruneFailedPreparations();
            PollPrograms(context);
            PollSubmittedFrames();
            result = std::move(requested);
            error.clear();
            return true;
        }
        const auto sameCoverage = [](const auto& a, const auto& b) {
            return a.flags == b.flags && a.cutoff == b.cutoff && a.baseAlpha == b.baseAlpha;
        };
        const auto supported = [](const SceneDrawInput& draw) {
            const auto& program = draw.material->generation->cooked.product.program;
            return program.surface || program.volume;
        };
        // Validate shared Material identities before mutating request revisions.
        std::map<std::uint64_t, const SceneDrawInput*> sources;
        for (const auto& draw : requested->Draws())
        {
            const auto [entry, inserted] = sources.emplace(draw.materialSlot, &draw);
            if (!draw.materialSlot || (!inserted && (!SamePinnedObject(entry->second->material, draw.material) ||
                                                     !sameCoverage(entry->second->coverage, draw.coverage))))
            {
                return Fail(error, "METEOR Scene Material slots must identify one exact instance and coverage per view.");
            }
        }
        std::erase_if(slots_, [&](const auto& item) {
            const auto [epoch, viewId, slot] = item.first;
            return viewId == view.viewId && (epoch != view.sceneEpoch || !sources.contains(slot));
        });
        for (const auto& [id, draw] : sources)
        {
            auto& slot = slots_[{view.sceneEpoch, view.viewId, id}];
            if (!slot)
            {
                slot = own::make_unique<Slot>();
            }
            if (!SamePinnedObject(slot->requested, draw->material) || !sameCoverage(slot->requestedCoverage, draw->coverage))
            {
                slot->requested = requested->MaterialOwner(*draw);
                slot->requestedCoverage = draw->coverage;
                slot->revision = ++selectionSerial_;
            }
            if (!supported(*draw))
            {
                return Fail(error, "METEOR Scene material has no Surface or Volume output.");
            }
            else
            {
                std::string preparationError;
                if (!RequestProgram(context, draw->material->generation, preparationError))
                {
                    selectionDeferred_ = preparationError == PreparationAdmissionError;
                    return Fail(error, preparationError);
                }
            }
        }
        PollPrograms(context);
        const auto backend = RHIShaderCompiler::GetOutput();
        // Every pass consumes the exact requested graph instance. Preparation
        // may defer a frame, but must never substitute a previous or native material.
        for (const auto& draw : requested->Draws())
        {
            if (!IsProgramReady(draw.material->generation, backend))
            {
                const auto failed = std::ranges::find_if(preparations_, [&](const auto& item)
                {
                    return SamePinnedObject(item->generation, draw.material->generation) && item->backend == backend
                        && !item->error.empty();
                });
                if (failed != preparations_.end())
                {
                    return Fail(error, (*failed)->error);
                }
                selectionDeferred_ = true;
                static auto nextReport = std::chrono::steady_clock::time_point{};
                if (std::chrono::steady_clock::now() >= nextReport)
                {
                    nextReport = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    std::printf("[meteor.scene.prepare] pending=%zu ready=%zu\n", preparations_.size(), programs_.size());
                    for (const auto& preparation : preparations_)
                        std::printf("[meteor.scene.prepare] checked=%u cooked=%u native=%u mesh=%zu error=%s\n",
                            unsigned(preparation->checked), unsigned(preparation->work && preparation->work->cooked),
                            unsigned(bool(preparation->program)), preparation->program ? preparation->program->meshPipelines.size() : 0,
                            preparation->error.c_str());
                }
                return Fail(error, "METEOR Scene requested material program is still preparing.");
            }
        }
        // Copy only after every requested program is ready. Geometry owners stay
        // shared, and each draw record is copied exactly once.
        auto selected = own::make_shared<SceneViewInput>(SceneViewInput::ConstructionKey{}, *requested);
        for (auto& draw : selected->draws_)
        {
            const auto& slot = slots_.at({view.sceneEpoch, view.viewId, draw.materialSlot});
            draw.selectionRevision = slot->revision;
        }
        // Only the cache's CPU record is pruned. Shared native handles stay in the
        // backend cache; global cache eviction remains a separate lifetime policy.
        // This drops only the cache pin. Frames/recordings keep their exact Program;
        // reference counts are not an in-use or GPU-completion decision.
        std::erase_if(programs_, [&](const auto& program) {
            return programs_.size() > 32 &&
                   std::ranges::none_of(slots_, [&](const auto& item) {
                       return (item.second->active && SamePinnedObject(item.second->active->generation, program->generation)) ||
                              (item.second->requested && SamePinnedObject(item.second->requested->generation, program->generation));
                   });
        });
        PruneFailedPreparations();
        result = std::move(selected);
        error.clear();
        return true;
    }

    bool SceneHost::PrepareResidency(const EnhancedFrameContext& context,
                                     const own::shared_owner<const SceneViewInput>& input, std::string& error) const
    {
        error.clear();
        if (!input || input->Draws().empty())
        {
            error.clear();
            return true;
        }
        if (!context.resources || !context.textureCache || context.resources->GetCurrentUploadRecordingId() == 0)
        {
            return Fail(error, "METEOR Scene texture residency needs an active owner upload recording.");
        }
        for (const auto& draw : input->Draws())
        {
            for (const auto& texture : draw.material->textures)
            {
                const auto failures = context.textureCache->GetUploadFailureCount();
                const auto entry = context.textureCache->GetOrUpload((texture.owner ? &*texture.owner.borrow() : nullptr),
                    context.TextureImage(texture.owner ? &*texture.owner : nullptr), error);
                if (!entry.IsValid() || !error.empty() || context.textureCache->GetUploadFailureCount() != failures)
                {
                    return Fail(error, error.empty() ? "METEOR Scene texture residency upload failed." : error);
                }
            }
        }
        error.clear();
        return true;
    }

    bool SceneHost::Prepare(const EnhancedFrameContext& context, own::shared_owner<const SceneViewInput> input,
                            RHITextureHandle environment, RHITextureHandle irradiance, RHITextureHandle prefiltered,
                            const EnhancedShadowData& shadow, const SceneHostBudget& budget, std::string& error,
                            std::uint64_t environmentGeneration, std::array<RHITextureHandle, 3> importance,
                            RHITextureHandle source)
    {
        ce::profile_scope profile{ce::marker<"MaterialGraphScenePrepare">()};
        if (device_) geometry_.BeginFrame(device_->GetCurrentUploadRecordingId());
        lookup_.ResetPreparationStatus();
        runtimeEffects_.ResetPreparationStatus();
        if (!input || input->Draws().empty())
        {
            frame_.reset();
            error.clear();
            return true;
        }
        if (!context.resources || !context.psoManager || !context.rootSignatures || !context.textureCache ||
            (device_ && device_ != context.resources) || context.resources->GetCurrentUploadRecordingId() == 0 ||
            input->View().frameId != context.frameId || input->View().sceneEpoch != context.sceneEpoch ||
            input->View().width != context.width || input->View().height != context.height)
        {
            return Fail(error, "METEOR Scene host needs the exact current view/frame/device services.");
        }
        if (std::uint64_t(context.width) * context.height > budget.pixels || budget.pixels > 4096u * 4096u ||
            input->Draws().size() > budget.draws || budget.draws > 4096)
        {
            return Fail(error, "METEOR Scene composition exceeds its configured viewport/draw budget.");
        }
        if (!device_)
        {
            LX::Runtime::CompiledCompute shader;
            if (!LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphMeshSurface.slang").string(),
                                             "LXTransformMesh", {}, {}, shader, error) ||
                !geometry_.Initialize(*context.resources, *context.rootSignatures, *context.psoManager,
                                      shader.stage.bytecode, error, std::move(shader.description)))
            {
                return false;
            }
            device_ = context.resources;
            device_->RegisterUploadTransactionListener(this);
        }
        auto candidate = own::make_shared<Frame>();
        candidate->device = device_;
        candidate->recording = device_->GetCurrentUploadRecordingId();
        candidate->descriptors = device_->GetDescriptorVersionToken();
        candidate->input = std::move(input);
        candidate->environment = environment;
        const auto emptyVisibility = RHIBindingDesc::Srv2D({}, RHIFormat::RG32Uint).OrNull();
        candidate->emptyVisibility = device_->CreateBindings({&emptyVisibility, 1});
        if (!candidate->emptyVisibility.IsValid()) return Fail(error, "METEOR empty layer visibility binding failed.");
        candidate->shadow = shadow.enabled;
        const std::array<std::byte, SceneVolumeConstantsBytes> emptyMedium{};
        candidate->emptyVolumeConstants = device_->UploadConstants(emptyMedium.data(), emptyMedium.size());
        candidate->emptyVolumeBuffer = device_->AllocateUpload({48, RHIUploadUsage::Raw, 16});
        if (!candidate->emptyVolumeConstants.IsValid() || !candidate->emptyVolumeBuffer.IsWritable())
        {
            return Fail(error, "METEOR Scene empty medium allocation failed.");
        }
        std::memset(candidate->emptyVolumeBuffer.cpuAddress, 0, 48);
        const RHIBindingDesc emptyVolumeBindings[]{RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull(),
                                                   RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull()};
        candidate->emptyVolumeBindings = device_->CreateBindings(emptyVolumeBindings);
        if (!candidate->emptyVolumeBindings.IsValid())
        {
            return Fail(error, "METEOR Scene empty medium bindings could not be allocated.");
        }
        const bool hasAlpha = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
            const auto& p = draw.material->generation->cooked.product.program;
            return p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u) != 0);
        });
        if (!lookup_.Prepare(context, candidate->input->View().viewId, environment, irradiance, prefiltered,
                             environmentGeneration, budget.lookupBytes, candidate->lookup, error, importance, source,
                             false, budget.lookupApproximate, false, budget.lookupRuntimeEvaluation))
        {
            return false;
        }
        if (hasAlpha && !lookup_.Prepare(context, candidate->input->View().viewId, environment, irradiance, prefiltered,
                                         environmentGeneration, budget.lookupBytes,
                                         candidate->alphaLookup, error, importance, source, true,
                                         budget.lookupApproximate, false, budget.lookupRuntimeEvaluation))
        {
            return false;
        }
        const bool hasSpecial = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
            return (draw.material->generation->cooked.product.program.features & 0x1800u) != 0;
        });
        if (hasSpecial && !budget.lookupRuntimeEvaluation)
        {
            std::array<RHIBindingDesc, 14> inputs;
            for (auto& input : inputs)
            {
                input = RHIBindingDesc::Srv2D({}, RHIFormat::RGBA32Float).OrNull();
            }
            candidate->emptyRuntimeInputs = device_->CreateBindings(inputs);
            const RHIBindingDesc background[]{RHIBindingDesc::Srv2D({}, RHIFormat::RGBA16Float).OrNull(),
                                               RHIBindingDesc::Srv2D({}, RHIFormat::R32Float).OrNull()};
            candidate->emptyRuntimeBackground = device_->CreateBindings(background);
            if (!candidate->emptyRuntimeInputs.IsValid() || !candidate->emptyRuntimeBackground.IsValid())
            {
                return Fail(error, "Offline special surface root bindings could not be allocated.");
            }
        }
        if (hasSpecial && !budget.lookupRuntimeEvaluation &&
            !subsurface_.Prepare(context, environment, budget.subsurfaceBytes, candidate->subsurface, error))
        {
            return false;
        }
        const bool hasTransmission = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
            return (draw.material->generation->cooked.product.program.features & 0x0800u) != 0;
        });
        const bool hasVolume = std::ranges::any_of(candidate->input->Draws(), [](const auto& draw) {
            return draw.material->generation->cooked.product.program.volume;
        });
        if (hasVolume && !volume_.Prepare(context, *candidate->input, environment, shadow, budget.volumeBytes,
                                          candidate->volume, error))
        {
            return false;
        }
        if (hasTransmission && !budget.lookupRuntimeEvaluation &&
            !refraction_.Prepare(context, candidate->input->ViewProjection(), environment, budget.refractionBytes,
                                 candidate->refraction, error, candidate->volume))
        {
            return false;
        }
        if (hasSpecial && budget.lookupRuntimeEvaluation &&
            !runtimeEffects_.Prepare(context, candidate->input->ViewProjection(), hasTransmission,
                                     bool(candidate->volume), budget.subsurfaceBytes, budget.refractionBytes,
                                     candidate->runtimeEffects, error))
        {
            return false;
        }
        for (unsigned enabled = 0; enabled < candidate->decalConstants.size(); ++enabled)
        {
            const std::array<std::uint32_t, 4> constants{enabled, 0, 0, 0};
            candidate->decalConstants[enabled] = device_->UploadConstants(constants.data(), sizeof(constants));
            if (!candidate->decalConstants[enabled].IsValid())
            {
                return Fail(error, "METEOR Scene Decal constants allocation failed.");
            }
        }
        std::uint32_t owner = 0x80000000u;
        auto geometryTable = std::make_shared<SceneFrameGeometry>();
        geometryTable->device = device_;
        std::vector<GpuGeometryVisibility::Candidate> visibilityCandidates;
        std::vector<GpuGeometryVisibility::Bin> visibilityBins;
        const bool indexedIndirect = device_->GetIndirectDrawCapabilities().indexedDraw;
        if (!indexedIndirect) return Fail(error, "METEOR Scene requires GPU indirect work; no CPU/direct surface route.");
        for (const auto& draw : candidate->input->Draws())
        {
            own::shared_owner<const Program> program;
            own::shared_owner<const RenderBindings> bindings;
            if (!PrepareProgram(context, *draw.material, program, error) ||
                !bindings_.Prepare(*device_, *context.textureCache, candidate->input->MaterialOwner(draw),
                    program->layout, bindings, error, candidate->input->MaterialPins(),
                    context.textureFramePins ? &*context.textureFramePins : nullptr))
            {
                return false;
            }
            if (program->hasVolume)
            {
                SceneVolumeBinding volume;
                volume.pipeline = program->volume.GetGeneration();
                if (!bindings_.Prepare(*device_, *context.textureCache, candidate->input->MaterialOwner(draw),
                    program->volumeLayout, volume.material, error, candidate->input->MaterialPins(),
                    context.textureFramePins ? &*context.textureFramePins : nullptr))
                {
                    return false;
                }
                const std::array<std::uint32_t, 4> constants{
                    static_cast<std::uint32_t>(candidate->volumeBindings.size()), 0, 0, 0};
                volume.constants = device_->UploadConstants(constants.data(), sizeof(constants));
                if (!volume.constants.IsValid())
                {
                    return Fail(error, "METEOR Scene Volume coefficient constants allocation failed.");
                }
                candidate->volumeBindings.push_back(std::move(volume));
            }
            SceneConstants constants{};
            constants.viewProjection = math::transpose(candidate->input->ViewProjection());
            const auto& eye = candidate->input->Surface().eye;
            constants.eye = math::vector4(eye[0], eye[1], eye[2], 1);
            constants.owner = ++owner;
            constants.geometryIdentity = {draw.geometryKey, draw.temporalObjectId, draw.temporalIncarnation,
                draw.temporalInstanceId, draw.materialSlot, draw.geometry->Source()->Geometry().sourceLodIndex,
                draw.geometry->Source()->SourcePrimitiveCount()};
            constants.width = context.width;
            constants.height = context.height;
            const auto& temporal = candidate->input->View().temporalFrame;
            const auto currentView = std::bit_cast<math::matrix4x4>(temporal.camera.viewMatrix);
            const auto currentProjection = std::bit_cast<math::matrix4x4>(temporal.camera.projectionMatrix);
            const auto currentToPrevious = std::bit_cast<math::matrix4x4>(temporal.camera.clipToPreviousClip);
            const auto currentVP = temporal.camera.valid ? currentView * currentProjection : candidate->input->ViewProjection();
            constants.temporalCurrentProjection = math::transpose(currentVP);
            constants.temporalPreviousProjection = math::transpose(temporal.camera.valid ? currentVP * currentToPrevious : currentVP);
            constants.temporalHistoryValid = draw.temporalHistoryValid && !temporal.reset;
            constants.coverage = draw.coverage.flags;
            const bool ordered =
                program->hasSurface && (draw.queue == SceneCoverage::Blended || program->hasTransmission);
            constants.padding[0] = ordered ? 1u : 0u;
            constants.padding[1] =
                (context.width + EnhancedForwardLighting::TileSize - 1) / EnhancedForwardLighting::TileSize;
            constants.padding[2] = ordered ? (draw.queue == SceneCoverage::Blended ? 1u : 2u) : 0u;
            constants.cutoff = draw.coverage.cutoff;
            constants.environment = environment.IsValid();
            constants.hasShadow = shadow.enabled;
            constants.shadowBlend = shadow.cascadeBlendBand;
            constants.shadowSplits = shadow.splitDepths;
            constants.shadowBias = shadow.bias;
            constants.cameraForward = shadow.cameraForward;
            for (unsigned i = 0; i < 3; ++i)
            {
                constants.shadowProjection[i] = math::transpose(shadow.lightViewProjection[i]);
            }
            if (context.lights)
            {
                const auto stored = (std::min)(context.lights->size(), std::size_t{64});
                constants.lightCount = static_cast<std::uint32_t>(ordered ? context.lights->size() : stored);
                std::copy_n(context.lights->begin(), stored, constants.lights);
            }
            const auto visibleOwner = device_->AllocateUpload({sizeof(constants.owner), RHIUploadUsage::BufferCopy, 256});
            if (!visibleOwner.IsValid() || !visibleOwner.IsWritable())
            {
                return Fail(error, "METEOR Scene visible owner allocation failed.");
            }
            std::memcpy(visibleOwner.cpuAddress, &constants.owner, sizeof(constants.owner));
            bool shadowEnabled{};
            std::array<RHIBufferSlice, 3> shadowConstants;
            std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> shadowMeshlets;
            std::uint32_t shadowMeshletBin{UINT32_MAX};
            if (program->hasSurface && shadow.enabled && draw.queue != SceneCoverage::Blended)
            {
                if (!RenderBindingCache::ValidatePass(*device_, *bindings, program->shadowLayout, error))
                {
                    return false;
                }
                shadowEnabled = true;
                for (unsigned cascade = 0; cascade < shadowConstants.size(); ++cascade)
                {
                    if (!indexedIndirect &&
                        !shadow_math::IntersectsClip({draw.shadowCenter, draw.shadowRadius},
                                                    shadow.lightViewProjection[cascade]))
                    {
                        continue;
                    }
                    SceneShadowConstants shadowValues;
                    shadowValues.viewProjection = math::transpose(shadow.lightViewProjection[cascade]);
                    shadowValues.coverage = draw.coverage.flags;
                    shadowValues.cutoff = draw.coverage.cutoff;
                    shadowValues.geometryIdentity = {draw.geometryKey, draw.temporalObjectId, draw.temporalIncarnation,
                        draw.temporalInstanceId, draw.materialSlot, 0, draw.shadowGeometry->Source()->SourcePrimitiveCount()};
                    shadowConstants[cascade] = device_->UploadConstants(&shadowValues, sizeof(shadowValues));
                    if (!shadowConstants[cascade].IsValid())
                    {
                        return Fail(error, "METEOR Scene shadow constants allocation failed.");
                    }
                }
            }
            for (const auto& chunk : draw.geometry->Chunks())
            {
                Frame::Draw item;
                item.program = program;
                item.blended = ordered;
                item.inputIndex = static_cast<std::size_t>(&draw - candidate->input->Draws().data());
                item.bindings = bindings;
                item.shadowEnabled = shadowEnabled;
                item.shadowConstants = shadowConstants;

                item.visibleOwner = visibleOwner;

                item.doubleSided = (draw.coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0;
                if (!geometry_.Prepare(*device_, chunk.input, item.geometry, error, true, candidate->input->View().temporalFrame.realFrameId ? candidate->input->View().temporalFrame.realFrameId : candidate->input->View().frameId, candidate->input->View().viewId))
                {
                    return false;
                }
                const auto chunkIndex = static_cast<std::size_t>(&chunk - draw.geometry->Chunks().data());
                if (draw.previousGeometry && draw.previousGeometry != draw.geometry && chunkIndex < draw.previousGeometry->Chunks().size())
                {
                    if (!geometry_.Prepare(*device_, draw.previousGeometry->Chunks()[chunkIndex].input,
                        item.previousGeometry, error, true, candidate->input->View().temporalFrame.realFrameId ? candidate->input->View().temporalFrame.realFrameId : candidate->input->View().frameId, candidate->input->View().viewId)) return false;
                }
                else item.previousGeometry = item.geometry;
                if (geometryTable->records.size() >= UINT32_MAX || item.geometry->MeshletCount() > (UINT32_MAX >> 7))
                    return Fail(error, "METEOR visibility address exceeds its frame geometry contract.");
                SceneFrameGeometry::Record record;
                record.identity = constants.geometryIdentity;
                record.chunkIndex = static_cast<uint32_t>(chunkIndex);
                record.owner = constants.owner;
                record.meshletCount = item.geometry->MeshletCount();
                record.vertexCount = static_cast<uint32_t>(chunk.input->Count());
                constants.geometryIdentity.reserved[0] = static_cast<uint32_t>(geometryTable->records.size());
                item.geometryRecord = constants.geometryIdentity.reserved[0];
                if (!SceneVisibilityContract::Reserve(record.meshletCount, geometryTable->nextPrimitiveAddress,
                    constants.geometryIdentity.reserved[1], constants.geometryIdentity.reserved[2]))
                    return Fail(error, "METEOR frame primitive address capacity exceeded; no truncated visibility IDs.");
                record.identity.reserved[1] = constants.geometryIdentity.reserved[1];
                record.identity.reserved[2] = constants.geometryIdentity.reserved[2];
                constants.padding[0] = ordered ? 1u : 0u;
                item.constants = device_->UploadConstants(&constants, sizeof(constants));
                if (ordered) {
                    constants.padding[0] = 2;
                    item.referenceConstants = device_->UploadConstants(&constants, sizeof(constants));
                }
                if (!item.constants.IsValid() || (ordered && !item.referenceConstants.IsValid()))
                    return Fail(error, "METEOR per-chunk geometry constants allocation failed.");
                record.identity.reserved[0] = 0;
                geometryTable->records.push_back(record);
                geometryTable->geometry.push_back(item.geometry);
                const auto& source = chunk.input->Geometry();
                // Per-draw transformed geometry and material constants still
                // define CPU bins for every surface, including ordered/special
                // and skinned materials. One candidate per bin prevents atomic
                // compaction from reordering the merged Code/Graph forward stream.
                // SceneViewInput sealed the pose/bounds; transformed geometry
                // remains a dependency of every raster consumer.
                // The compiler accepts Surface and/or Volume closures; emission
                // belongs to Surface, not a separate color-only raster program.
                // Surface color/lookup passes require hasSurface. Special
                // capture entry points key on their feature bits instead, so
                // cover those entries too. Volume-only programs emit no indexed
                // raster work and keep their existing compute/composite route.
                const bool rasterGeometry = program->hasSurface || program->hasSpecial;
                if (rasterGeometry && !ordered && !candidate->UsesMeshlets(item))
                    return Fail(error, "METEOR opaque surface requires supported cooked meshlets; reimport the asset.");
                if (indexedIndirect && rasterGeometry)
                {
                    item.visibilityBin = static_cast<std::uint32_t>(visibilityBins.size());
                    item.visibleIdOffset = item.visibilityBin * GpuGeometryVisibility::kOutputAlignment;
                    visibilityBins.push_back(candidate->UsesMeshlets(item)
                        ? GpuGeometryVisibility::Bin{}
                        : GpuGeometryVisibility::Bin{source.indexCount, 0, 0, 0});
                    visibilityCandidates.push_back({
                        math::vector4{draw.shadowCenter.x, draw.shadowCenter.y, draw.shadowCenter.z, draw.shadowRadius},
                        item.visibilityBin, constants.owner, item.visibleIdOffset,
                        (ordered ? GpuGeometryVisibility::kNoOcclusion : 0u) |
                            (candidate->UsesMeshlets(item) ? GpuGeometryVisibility::kMeshletOwner : 0u)});
                }

                const auto bytes = std::size_t(source.indexCount) * sizeof(std::uint32_t);
                item.indices = item.geometry->Indices();
                if (!item.indices.IsValid())
                {
                    item.indices = device_->AllocateUpload({bytes, RHIUploadUsage::IndexData, alignof(std::uint32_t)});
                    if (!item.indices.IsValid() || !item.indices.cpuAddress)
                    {
                        return Fail(error, "METEOR Scene index upload failed.");
                    }
                    std::memcpy(item.indices.cpuAddress, source.indexData, bytes);
                }
                if (shadowEnabled && !draw.geometryLod)
                {
                    candidate->shadowDraws.push_back(item);
                }
                candidate->draws.push_back(std::move(item));
            }
            if (shadowEnabled && draw.geometryLod)
            {
                for (const auto& chunk : draw.shadowGeometry->Chunks())
                {
                    Frame::Draw item;
                    item.program = program;
                    item.bindings = bindings;
                    item.doubleSided = (draw.coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0;
                    item.blended = ordered;
                    item.shadowEnabled = true;
                    item.shadowConstants = shadowConstants;
                    if (!geometry_.Prepare(*device_, chunk.input, item.geometry, error, true, candidate->input->View().temporalFrame.realFrameId ? candidate->input->View().temporalFrame.realFrameId : candidate->input->View().frameId, candidate->input->View().viewId))
                    {
                        return false;
                    }
                    // Shadow-only LOD0 geometry is never consumed by the motion
                    // pass and does not share the visible LOD's chunk indexing.
                    const auto& source = chunk.input->Geometry();

                    item.indices = item.geometry->Indices();
                    if (!item.indices.IsValid())
                    {
                        const auto bytes = size_t(source.indexCount) * sizeof(uint32_t);
                        item.indices = device_->AllocateUpload({bytes, RHIUploadUsage::IndexData, alignof(uint32_t)});
                        if (!item.indices.IsWritable())
                        {
                            return Fail(error, "METEOR Scene shadow LOD0 index upload failed.");
                        }
                        std::memcpy(item.indices.cpuAddress, source.indexData, bytes);
                    }
                    candidate->shadowDraws.push_back(std::move(item));
                }
            }
        }
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> cameraGeometry;
        for (auto& draw : candidate->draws)
            if (candidate->UsesMeshlets(draw)) {
                draw.cameraMeshletBin = static_cast<uint32_t>(cameraGeometry.size());
                cameraGeometry.push_back(draw.geometry);
            }
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> previousCameraHistory;
        if (!cameraGeometry.empty())
        {
            const auto& view = candidate->input->View();
            auto historyCamera = view.camera;
            if (view.temporalFrame.camera.valid)
                std::memcpy(&historyCamera.projection, view.temporalFrame.camera.projectionMatrix.data(), sizeof(historyCamera.projection));
            std::uint64_t completed;
            { std::lock_guard lock(recordingMutex_); completed = completed_; }
            // Only completed, accepted history can enter another recording.
            // Keep the newest completed view plus its still-submitted successors.
            std::erase_if(cameraHistories_, [&](const auto& entry) { return entry.first.first != view.sceneEpoch; });
            auto& histories = cameraHistories_[{view.sceneEpoch, view.viewId}];
            std::uint64_t newest{};
            for (const auto& item : histories)
                if (item.pyramid->completion <= completed) newest = (std::max)(newest, item.pyramid->source.frameId);
            std::erase_if(histories, [&](const auto& item) {
                return item.pyramid->completion <= completed && item.pyramid->source.frameId < newest;
            });
            for (const auto& item : histories)
            {
                const auto& h = *item.pyramid;
                if (h.source.frameId == newest && h.source.frameId < view.frameId && h.completion <= completed &&
                    h.source.width == view.width && h.source.height == view.height &&
                    h.revision == view.historyRevision && historyCamera.HasSameTemporalHistory(item.camera) &&
                    item.temporalRevision == view.temporalFrame.historyRevision &&
                    (!view.temporalFrame.realFrameId || !view.temporalFrame.reset))
                    previousCameraHistory = GpuGeometryOcclusion::ReadHistory(context,
                        candidate->input->ViewProjection(), item.pyramid);
            }
            if (!cameraOcclusion_.PrepareHistory(context, candidate->input->ViewProjection(),
                view.viewId, view.historyRevision, candidate->nextCameraHistory, error)) return false;
        }
        if (!cameraGeometry.empty() &&
            (!visibility_.PrepareMeshlets(context, candidate->input->ViewProjection(), cameraGeometry,
                candidate->cameraDepthMeshlets, error, false, previousCameraHistory) ||
             !visibility_.PrepareMeshlets(context, candidate->input->ViewProjection(), cameraGeometry,
                candidate->cameraMeshlets, error, true, {}, candidate->cameraDepthMeshlets))) return false;
        if (!visibility_.Prepare(context, candidate->input->ViewProjection(), visibilityCandidates, visibilityBins,
                                 candidate->visibility, error, true))
        {
            return false;
        }
        if (!geometryTable->Upload(error)) return false;
        candidate->geometryTable = std::move(geometryTable);
        if (std::any_of(candidate->draws.begin(), candidate->draws.end(), [](const auto& draw) {
            return draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended;
        })) {
            auto hybrid = std::make_shared<SceneHybridVisibility>();
            hybrid->geometry = candidate->geometryTable;
            hybrid->visible = candidate->cameraMeshlets;
            hybrid->records.resize(candidate->geometryTable->records.size());
            for (const auto& draw : candidate->draws) {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
                const auto coverage = candidate->input->Draws()[draw.inputIndex].coverage.flags;
                hybrid->records[draw.geometryRecord] = {candidate->cameraMeshlets->MeshletOutputOffset(draw.cameraMeshletBin),
                    1u | ((coverage & 2u) ? 0u : 2u) | (draw.doubleSided ? 4u : 0u), 0u, 0u};
            }
            if (!hybrid->Prepare(context, candidate->input->ViewProjection(), hybridPipelines_, error)) return false;
            candidate->hybridVisibility = std::move(hybrid);
        }
        candidate->inputRanges.resize(candidate->input->Draws().size());
        for (size_t i = 0; i < candidate->draws.size(); ++i)
        {
            auto& range = candidate->inputRanges[candidate->draws[i].inputIndex];
            if (range.second == 0)
            {
                range.first = i;
            }
            ++range.second;
        }
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> shadowMeshletGeometry;
        for (auto& draw : candidate->shadowDraws) {
            if (!draw.shadowEnabled) continue;
            if (!candidate->UsesMeshlets(draw)) return Fail(error, "METEOR shadow requires cooked meshlets and supported mesh indirect dispatch; reimport the asset.");
            draw.shadowMeshletBin = static_cast<std::uint32_t>(shadowMeshletGeometry.size());
            shadowMeshletGeometry.push_back(draw.geometry);
        }
        if (!shadowMeshletGeometry.empty()) for (unsigned cascade = 0; cascade < 3; ++cascade) {
            std::shared_ptr<const GpuGeometryVisibility::Frame> culling;
            if (!visibility_.PrepareMeshlets(context, shadow.lightViewProjection[cascade], shadowMeshletGeometry,
                culling, error)) return false;
            if (!culling) return Fail(error, "Shadow meshlet compaction was not prepared.");
            for (auto& draw : candidate->shadowDraws) {
                if (draw.shadowMeshletBin == UINT32_MAX) continue;
                draw.shadowMeshlets[cascade] = culling;
                SceneShadowConstants constants{};
                std::memcpy(&constants, draw.shadowConstants[cascade].cpuAddress, sizeof(constants));

                draw.shadowConstants[cascade] = device_->UploadConstants(&constants, sizeof(constants));
                if (!draw.shadowConstants[cascade].IsValid()) return Fail(error, "Shadow compacted constants allocation failed.");
            }
        }
        if (!cameraGeometry.empty() || !shadowMeshletGeometry.empty() ||
            std::ranges::any_of(candidate->draws, [](const auto& draw) { return draw.program->hasSurface; }))
        {
            if (!indirectCommands_.GetGeneration())
            {
                const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0),
                    RHILayout::Srv(1), RHILayout::Srv(2), RHILayout::UavBufferTable(1, 0)};
                const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
                LX::Runtime::CompiledCompute compiled;
                RHIShaderCompileOptions options; options.strictMath = true;
                if (!layout.IsValid() || !LX::Runtime::CompileCompute(
                    RHIShaderSource::Resolve("MaterialGraphIndirect.slang").string(),
                    "LXBuildIndirectCommands", {}, options, compiled, error)) return false;
                RHIComputePipelineDesc desc; desc.layout = layout;
                desc.csBytecode = compiled.stage.bytecode.Data(); desc.csSize = compiled.stage.bytecode.Size();
                if (!indirectCommands_.Create(*context.psoManager, desc, std::move(compiled.description), error)) return false;
            }
            // GBuffer bytecode can be identical while the color/lookup shader
            // differs. A bucket must share the complete cooked program identity.
            std::map<std::pair<std::string, bool>, std::vector<std::size_t>> groups;
            for (std::size_t index = 0; index < candidate->draws.size(); ++index)
            {
                const auto& draw = candidate->draws[index];
                if (candidate->UsesMeshlets(draw))
                    groups[{draw.program->generation->cooked.product.program.semanticKey, draw.doubleSided}].push_back(index);
            }
            const auto prepare = [&](bool depth, std::shared_ptr<const IndirectMaterialBatch>& result,
                std::optional<unsigned> cascade = {}) {
                auto batch = std::make_shared<IndirectMaterialBatch>();
                batch->name = cascade ? "METEOR.Scene.Commands.Shadow." + std::to_string(*cascade)
                    : depth ? "METEOR.Scene.Commands.Depth" : "METEOR.Scene.Commands.Surface";
                batch->device = device_;
                batch->pipeline = indirectCommands_.GetGeneration();
                auto& draws = cascade ? candidate->shadowDraws : candidate->draws;
                batch->visibility = cascade ? draws[groups.begin()->second.front()].shadowMeshlets[*cascade]
                    : depth ? candidate->cameraDepthMeshlets : candidate->cameraMeshlets;
                std::vector<std::uint32_t> words;
                std::vector<std::array<std::uint32_t, 4>> records;
                std::uint32_t outputBytes{};
                for (const auto& [key, indices] : groups)
                {
                    IndirectMaterialBatch::Bucket bucket;
                    bucket.representative = indices.front(); bucket.offset = outputBytes;
                    bucket.count = static_cast<std::uint32_t>(indices.size());
                    for (const auto index : indices)
                    {
                        auto& draw = draws[index];
                        draw.materialBucket = static_cast<std::uint32_t>(batch->buckets.size());
                        draw.bucketRepresentative = index == indices.front();
                        const auto& layout = cascade ? draw.program->shadowLayout : draw.program->layout;
                        std::vector<std::uint32_t> roots;
                        const auto first = static_cast<std::uint32_t>(words.size() * 4);
                        const auto append = [&](std::uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                            std::uint64_t value{};
                            if (!device_->EncodeIndirectBuffer(slice, value) || (cbv && value % 256)) return false;
                            roots.push_back(slot | (cbv ? 0x80000000u : 0));
                            words.push_back(static_cast<std::uint32_t>(value));
                            words.push_back(static_cast<std::uint32_t>(value >> 32));
                            return true;
                        };
                        const unsigned ownerRoot = 15 + (draw.program->hasSpecial ? 2 : 0) +
                            (draw.program->hasTransmission ? 1 : 0);
                        const auto bin = cascade ? draw.shadowMeshletBin : draw.cameraMeshletBin;
                        const bool geometryRoots = cascade
                            ? append(0, draw.shadowConstants[*cascade], true) &&
                                append(1, RHIBufferSlice::Whole(draw.geometry->Buffer())) &&
                                append(draw.program->shadowMeshletRoot, draw.geometry->Meshlets()) &&
                                append(3, batch->visibility->VisibleIds(
                                    batch->visibility->MeshletOutputOffset(bin), draw.geometry->MeshletCount() + 1))
                            : append(0, draw.constants, true) &&
                                append(1, RHIBufferSlice::Whole(draw.geometry->Buffer())) &&
                                append(ownerRoot, depth ? draw.visibleOwner : candidate->visibility->VisibleIds(draw.visibleIdOffset, 1)) &&
                                append(draw.program->meshletRoot, draw.geometry->Meshlets()) &&
                                append(draw.program->temporalPreviousRoot + 1, batch->visibility->VisibleIds(
                                    batch->visibility->MeshletOutputOffset(bin), draw.geometry->MeshletCount() + 1));
                        if (!geometryRoots ||
                            (layout.uniformSlot && !append(*layout.uniformSlot, draw.bindings->uniforms, true)) ||
                            (layout.descriptorIndexSlot && !append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true)))
                            return Fail(error, "LX indirect root descriptor preparation failed; no CPU submission fallback.");
                        if (bucket.roots.empty()) bucket.roots = roots;
                        if (bucket.roots != roots) return Fail(error, "LX shader bucket root ABI differs between instances.");
                        records.push_back({first, outputBytes, static_cast<std::uint32_t>(roots.size() * 2),
                            static_cast<std::uint32_t>(batch->visibility->ArgsOffset(bin))});
                        outputBytes += static_cast<std::uint32_t>(roots.size() * 8 + 12);
                    }
                    batch->buckets.push_back(std::move(bucket));
                }
                if (!batch->Upload(words, records, outputBytes, error)) return false;
                result = std::move(batch);
                return true;
            };
            if (!groups.empty()) {
                if (!prepare(false, candidate->materialCommands) || !prepare(true, candidate->depthCommands)) return false;
                std::printf("[meteor.material-buckets] commands=%u buckets=%zu cpuInstanceSubmissions=0\n",
                    candidate->materialCommands->commandCount, candidate->materialCommands->buckets.size());
            }
            const auto needsMaterialTiles = [&](const auto& group) {
                const auto& draw = candidate->draws[group.second.front()];
                return draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended;
            };
            if (std::ranges::any_of(groups, needsMaterialTiles)) {
                const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::SrvTable(1,0),
                    RHILayout::Srv(1), RHILayout::Srv(2), RHILayout::UavBufferTable(2,0), RHILayout::Srv(3)};
                const auto layout = context.rootSignatures->GetOrCreate({params,{}},error);
                constexpr const char* entries[]{"ResetMaterialWork","CountMaterialTiles","BuildMaterialRanges","ScatterMaterialTiles"};
                auto work = std::make_shared<SceneMaterialWork>();
                work->device = device_; work->width = context.width; work->height = context.height;
                work->recordCount = static_cast<uint32_t>(candidate->geometryTable->records.size());
                work->geometry = candidate->geometryTable;
                for (unsigned stage = 0; stage < 4; ++stage) {
                    if (!materialWorkPipelines_[stage].GetGeneration()) {
                        LX::Runtime::CompiledCompute compiled;
                        RHIShaderCompileOptions options; options.strictMath = true;
                        if (!layout.IsValid() || !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphWork.slang").string(),
                            entries[stage], {}, options, compiled, error)) return false;
                        RHIComputePipelineDesc desc; desc.layout = layout;
                        desc.csBytecode = compiled.stage.bytecode.Data(); desc.csSize = compiled.stage.bytecode.Size();
                        if (!materialWorkPipelines_[stage].Create(*context.psoManager, desc, std::move(compiled.description), error)) return false;
                    }
                    work->pipelines[stage] = materialWorkPipelines_[stage].GetGeneration();
                }
                std::vector<uint32_t> words;
                std::vector<std::array<uint32_t,4>> records;
                uint32_t bytes{};
                for (const auto& group : groups) {
                    if (!needsMaterialTiles(group)) continue;
                    const auto& indices = group.second;
                    SceneMaterialWork::Bucket bucket;
                    bucket.representative = indices.front(); bucket.offset = bytes;
                    bucket.count = static_cast<uint32_t>(indices.size());
                    for (const auto index : indices) {
                        auto& draw = candidate->draws[index];
                        draw.materialTileBucket = static_cast<uint32_t>(work->buckets.size());
                        const auto first = static_cast<uint32_t>(words.size() * 4);
                        std::vector<uint32_t> roots;
                        const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                            uint64_t address{};
                            if (!device_->EncodeIndirectBuffer(slice,address) || (cbv && address % 256)) return false;
                            roots.push_back(slot | (cbv ? 0x80000000u : 0));
                            words.push_back(static_cast<uint32_t>(address)); words.push_back(static_cast<uint32_t>(address >> 32));
                            return true;
                        };
                        const auto& bindings = draw.program->layout;
                        if (!append(0,draw.constants,true) || !append(1,RHIBufferSlice::Whole(draw.geometry->Buffer())) ||
                            !append(draw.program->meshletRoot,draw.geometry->Meshlets()) ||
                            (bindings.uniformSlot && !append(*bindings.uniformSlot,draw.bindings->uniforms,true)) ||
                            (bindings.descriptorIndexSlot && !append(*bindings.descriptorIndexSlot,draw.bindings->descriptorIndices,true)))
                            return Fail(error,"METEOR material tile root template failed.");
                        if (bucket.roots.empty()) bucket.roots = roots;
                        if (bucket.roots != roots) return Fail(error,"METEOR material tile bucket ABI mismatch.");
                        records.push_back({first,bytes,static_cast<uint32_t>(roots.size()*2),draw.geometryRecord});
                        bytes += static_cast<uint32_t>(roots.size()*8 + 12);
                    }
                    work->buckets.push_back(std::move(bucket));
                }
                if (!work->Upload(words,records,bytes,error)) return false;
                candidate->materialWork = std::move(work);
            }
            groups.clear();
            for (std::size_t index = 0; index < candidate->shadowDraws.size(); ++index) {
                const auto& draw = candidate->shadowDraws[index];
                if (draw.shadowEnabled)
                    groups[{draw.program->generation->cooked.product.program.semanticKey, draw.doubleSided}].push_back(index);
            }
            if (!groups.empty()) for (unsigned cascade = 0; cascade < 3; ++cascade) {
                if (!prepare(false, candidate->shadowCommands[cascade], cascade)) return false;
                std::printf("[meteor.shadow-buckets] cascade=%u commands=%u buckets=%zu cpuInstanceSubmissions=0\n",
                    cascade, candidate->shadowCommands[cascade]->commandCount, candidate->shadowCommands[cascade]->buckets.size());
            }
            // Keep replay order, including equal-depth and alpha surfaces. Merge
            // only adjacent matching PSOs; no camera visibility filter changes correspondence.
            auto replay = std::make_shared<IndirectMaterialBatch>();
            replay->name = "METEOR.Scene.Commands.Temporal";
            replay->device = device_;
            replay->pipeline = indirectCommands_.GetGeneration();
            replay->indexedReplay = true;
            std::vector<uint32_t> replayWords;
            std::vector<std::array<uint32_t, 4>> replayRecords;
            RHIPipelineHandle previousPipeline{};
            for (std::size_t index = 0; index < candidate->draws.size(); ++index) {
                const auto& draw = candidate->draws[index];
                if (!draw.program->hasSurface) continue;
                const auto pipeline = draw.program->temporal[draw.doubleSided].GetHandle();
                if (replay->buckets.empty() || pipeline != previousPipeline) {
                    IndirectMaterialBatch::Bucket bucket;
                    bucket.representative = index;
                    bucket.offset = static_cast<uint32_t>(replayWords.size() * 4);
                    replay->buckets.push_back(std::move(bucket));
                    previousPipeline = pipeline;
                }
                auto& bucket = replay->buckets.back();
                const auto first = static_cast<uint32_t>(replayWords.size() * 4);
                std::vector<uint32_t> roots;
                const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false) {
                    uint64_t address{};
                    if (!device_->EncodeIndirectBuffer(slice, address) || (cbv && address % 256)) return false;
                    roots.push_back(slot | (cbv ? 0x80000000u : 0));
                    replayWords.push_back(uint32_t(address)); replayWords.push_back(uint32_t(address >> 32));
                    return true;
                };
                const auto& layout = draw.program->layout;
                const unsigned ownerRoot = 15 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0);
                if (!append(0, draw.constants, true) || !append(1, RHIBufferSlice::Whole(draw.geometry->Buffer())) ||
                    !append(ownerRoot, draw.visibleOwner) ||
                    !append(draw.program->temporalPreviousRoot, RHIBufferSlice::Whole(draw.previousGeometry->Buffer())) ||
                    (layout.uniformSlot && !append(*layout.uniformSlot, draw.bindings->uniforms, true)) ||
                    (layout.descriptorIndexSlot && !append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true)))
                    return Fail(error, "LX temporal indirect root preparation failed.");
                if (bucket.roots.empty()) bucket.roots = roots;
                if (bucket.roots != roots) return Fail(error, "LX temporal bucket root ABI mismatch.");
                std::array<uint32_t, 4> indexView{};
                if (!device_->EncodeIndirectIndexBuffer(draw.indices, RHIFormat::R32Uint, indexView))
                    return Fail(error, "LX temporal indirect index view unavailable; no direct replay fallback.");
                replayWords.insert(replayWords.end(), indexView.begin(), indexView.end());
                const uint32_t args[]{draw.geometry->Input()->Geometry().indexCount, 1, 0, 0, 0};
                replayWords.insert(replayWords.end(), std::begin(args), std::end(args));
                replayRecords.push_back({first, first, uint32_t(roots.size() * 2 + 9), 0});
                ++bucket.count;
            }
            if (!replayRecords.empty()) {
                if (!replay->Upload(replayWords, replayRecords, uint32_t(replayWords.size() * 4), error)) return false;
                candidate->temporalCommands = std::move(replay);
            }
        }

        // Captures and tiled effects preserve each sorted layer boundary. Within
        // a layer, every geometry chunk shares material/scene constants and PSO;
        // only geometry, visible-owner address and indexed work vary per command.
        auto indexed = std::make_shared<IndirectMaterialBatch>();
        indexed->name = "METEOR.Scene.Commands.Layer";
        indexed->device = device_;
        indexed->pipeline = indirectCommands_.GetGeneration();
        indexed->visibility = candidate->visibility;
        indexed->indexedReplay = indexed->indexedVisibility = true;
        std::vector<uint32_t> indexedWords;
        std::vector<std::array<uint32_t, 4>> indexedRecords;
        for (const auto [first, count] : candidate->inputRanges)
        {
            uint32_t bucketIndex = UINT32_MAX;
            for (size_t index = first; index < first + count; ++index)
            {
                auto& draw = candidate->draws[index];
                if (draw.blended && draw.program->hasSurface &&
                    (draw.program->hasSpecial || candidate->alphaLookup->RequiresCapture()))
                {
                    if (bucketIndex == UINT32_MAX) {
                        bucketIndex = static_cast<uint32_t>(indexed->buckets.size());
                        IndirectMaterialBatch::Bucket bucket;
                        bucket.representative = index;
                        bucket.offset = static_cast<uint32_t>(indexedWords.size() * 4);
                        bucket.roots = {1u, 15u + (draw.program->hasSpecial ? 2u : 0u) +
                            (draw.program->hasTransmission ? 1u : 0u)};
                        indexed->buckets.push_back(std::move(bucket));
                    }
                    auto& bucket = indexed->buckets[bucketIndex];
                    const auto offset = static_cast<uint32_t>(indexedWords.size() * 4);
                    for (const auto slice : {RHIBufferSlice::Whole(draw.geometry->Buffer()),
                        candidate->visibility->VisibleIds(draw.visibleIdOffset, 1)}) {
                        uint64_t address{};
                        if (!device_->EncodeIndirectBuffer(slice, address))
                            return Fail(error, "METEOR layer indirect root preparation failed.");
                        indexedWords.push_back(uint32_t(address)); indexedWords.push_back(uint32_t(address >> 32));
                    }
                    std::array<uint32_t, 4> indexView{};
                    if (!device_->EncodeIndirectIndexBuffer(draw.indices, RHIFormat::R32Uint, indexView))
                        return Fail(error, "METEOR layer indirect index view unavailable.");
                    indexedWords.insert(indexedWords.end(), indexView.begin(), indexView.end());
                    indexedWords.insert(indexedWords.end(), 5, 0);
                    indexedRecords.push_back({offset, offset, 13u,
                        static_cast<uint32_t>(candidate->visibility->ArgsOffset(draw.visibilityBin))});
                    draw.indexedBucket = bucketIndex;
                    draw.bucketRepresentative = bucket.count++ == 0;
                }
            }
        }
        if (!indexedRecords.empty()) {
            if (!indexed->Upload(indexedWords, indexedRecords, uint32_t(indexedWords.size() * 4), error)) return false;
            candidate->indexedCommands = std::move(indexed);
        }
        // The replay walks every caster vertex for three cascades. Sample at most
        // one prepared frame every two seconds so it cannot stall the render thread.
        static std::atomic<std::int64_t> lastAuditMs{};
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const auto lastMs = lastAuditMs.load();
        if (shadow.enabled && ShadowMeshletAuditEnabled() && (lastMs == 0 || nowMs - lastMs >= 2000))
        {
            lastAuditMs.store(nowMs);
            std::array<ShadowMeshletAudit, 3> audits{};
            std::uint32_t indexed{}, skinned{};
            for (const auto& item : candidate->shadowDraws)
            {
                if (!item.shadowEnabled)
                {
                    continue;
                }
                if (!candidate->UsesMeshlets(item))
                {
                    ++indexed;
                    continue;
                }
                const auto& input = *item.geometry->Input();
                if (!input.Bones().empty())
                {
                    ++skinned;
                    continue;
                }
                for (unsigned cascade = 0; cascade < audits.size(); ++cascade)
                {
                    AuditShadowMeshlets(input, shadow.lightViewProjection[cascade], audits[cascade]);
                }
            }
            for (unsigned cascade = 0; cascade < audits.size(); ++cascade)
            {
                const auto& audit = audits[cascade];
                std::printf("[meteor.meshlets.audit] frame=%llu cascade=%u draws=%llu visibleDraws=%llu meshlets=%llu "
                            "rejected=%llu triangles=%llu rejectedTriangles=%llu indexed=%u skinned=%u\n",
                            static_cast<unsigned long long>(context.frameId), cascade,
                            static_cast<unsigned long long>(audit.draws), static_cast<unsigned long long>(audit.visibleDraws),
                            static_cast<unsigned long long>(audit.meshlets), static_cast<unsigned long long>(audit.rejected),
                            static_cast<unsigned long long>(audit.triangles),
                            static_cast<unsigned long long>(audit.rejectedTriangles), indexed, skinned);
            }
            std::fflush(stdout);
        }
        try
        {
            if (!candidate->PrepareBlended(indirectCommands_.GetGeneration(), error)) return false;
            candidate->CheckCurrent();
        }
        catch (const std::exception& exception)
        {
            return Fail(error, exception.what());
        }
        // Freeze before any durable publication. Recording and graph callbacks
        // share this exact frame; no mutable candidate alias survives the handoff.
        own::shared_owner<const Frame> published(std::move(candidate));
        decltype(recordings_) retired;
        {
            ce::profile_scope retire{ce::marker<"MaterialFrameRetirement">()};
            std::lock_guard lock(recordingMutex_);
            recordings_[published->recording].owners.push_back(published);
            for (auto it = recordings_.begin(); it != recordings_.end();)
            {
                const auto& recording = it->second;
                if (it->first != published->recording && !recording.publication && recording.accepted &&
                    recording.completion != 0 && recording.completion <= completed_)
                {
                    retired.insert(recordings_.extract(it++));
                }
                else
                {
                    ++it;
                }
            }
        }
        {
            ce::profile_scope replace{ce::marker<"MaterialFrameReplace">()};
            frame_ = std::move(published);
        }
        // Resource/ticket destruction may call backend code; never under the
        // recording mutex, including completed entries replaced by another view.
        error.clear();
        return true;
    }

    // METEOR.Scene.Cull node: every visibility decision of the frame. Shadow
    // cascades get compute meshlet lists; the camera gets the occluder depth
    // prepass, its current-frame pyramid and instance culling. Raster consumers
    // only read the declared lists.
    void SceneHost::DeclareCulling(EnhancedRenderGraph& graph) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        ce::profile_scope profile{ce::marker<"MaterialDeclareCulling">()};
        const auto frame = frame_;
        if (!frame)
        {
            return;
        }
        frame->CheckCurrent();
        if (frame->cullDeclared)
        {
            throw std::runtime_error("METEOR Scene culling requires one declaration per prepared frame.");
        }
        frame->cullDeclared = true;
        if (frame->shadow)
        {
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                std::vector<std::shared_ptr<const GpuGeometryVisibility::Frame>> meshlets;
                for (const auto& draw : frame->shadowDraws)
                {
                    if (draw.shadowMeshlets[cascade]) meshlets.push_back(draw.shadowMeshlets[cascade]);
                }
                GpuGeometryVisibility::Frame::DeclareMeshletBatch(graph, meshlets);
                if (frame->shadowCommands[cascade]) frame->shadowCommands[cascade]->Declare(graph, frame->shadowCommands[cascade]);
            }
        }
        if (frame->draws.empty())
        {
            return;
        }
        RequireGeometry(*frame, graph);
        if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            throw std::runtime_error("METEOR Scene culling requires versioned current-depth HZB wiring.");
        if (frame->cameraDepthMeshlets) {
            frame->cameraDepthMeshlets->DeclareFirstStage(graph);
        }
        if (frame->depthCommands) frame->depthCommands->Declare(graph, frame->depthCommands);
        RGHandle occluderDepth;
        const bool depthPrepass = frame->visibility &&
            graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
        if (depthPrepass)
        {
            // Exact current-frame coverage/pose/LOD, with no camera/HZB-filtered
            // arguments. The pyramid reads this version before the main write.
            RGTextureDesc depthDescription;
            depthDescription.width = frame->input->View().width;
            depthDescription.height = frame->input->View().height;
            depthDescription.format = RHIFormat::D32Float;
            depthDescription.allowDepthStencil = true;
            depthDescription.name = "METEOR.Scene.OccluderDepth";
            // Keep main depth untouched so Less and first-primitive tie ownership
            // remain identical to the indexed baseline after HZB filtering.
            occluderDepth = graph.Write(graph.CreateTexture(depthDescription));
            std::vector<EnhancedRenderGraph::RGPassUsage> depthUses;
            depthUses.push_back({occluderDepth, RHIResourceState::DepthWrite, RGAccessMode::Write});
            for (const auto& draw : frame->draws)
            {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                {
                    continue;
                }
                depthUses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
                if (frame->UsesMeshlets(draw))
                {
                    depthUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    depthUses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
                }
            }
            if (frame->cameraDepthMeshlets) frame->cameraDepthMeshlets->AddVisibleIdsRead(graph, depthUses);
            if (frame->depthCommands) frame->depthCommands->AddRead(graph, depthUses);
            NormalizeSceneReads(graph, depthUses);
            graph.AddPass("METEOR.Scene.OccluderDepth", depthUses, [frame, depthHandle = occluderDepth](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto depthTarget = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depthHandle), RHIFormat::D32Float);
                const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depthTarget);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR Scene occluder depth target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                encoder.ClearDepthTarget(targets, 1.f);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                for (const auto& draw : frame->RasterDrawsFor())
                {
                    if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                    {
                        continue;
                    }
                    encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->depth[draw.doubleSided].GetHandle()));
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                    {
                        throw std::runtime_error(error);
                    }
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    const unsigned ownerRoot = 15 + (draw.program->hasSpecial ? 2 : 0) +
                        (draw.program->hasTransmission ? 1 : 0);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, ownerRoot, draw.visibleOwner);
                    if (frame->UsesMeshlets(draw))
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->meshletRoot, draw.geometry->Meshlets());
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->temporalPreviousRoot + 1,
                            frame->cameraDepthMeshlets->VisibleIds(
                                frame->cameraDepthMeshlets->MeshletOutputOffset(draw.cameraMeshletBin),
                                draw.geometry->MeshletCount() + 1));
                        frame->depthCommands->Draw(encoder, draw.materialBucket);
                    }
                    else
                    {
                        encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                        encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
                    }
                }
            });
        }
        if (frame->visibility)
        {
            // One immutable pyramid shared by owner and meshlet culling. Its
            // producer uses only the independent frustum list, never HZB output.
            frame->visibility->DeclareWithOcclusion(graph, occluderDepth);
            if (frame->cameraMeshlets)
                frame->cameraMeshlets->DeclareWithOcclusion(graph, frame->visibility->PreparedOcclusionPyramid());
        }
        for (const auto& batch : {frame->materialCommands, frame->indexedCommands, frame->temporalCommands})
            if (batch) batch->Declare(graph, batch);
        for (const auto& range : frame->blendedRanges)
            for (const auto& batch : range.commands) if (batch) batch->Declare(graph, batch);
    }

    void SceneHost::RequireGeometry(const Frame& frame, const EnhancedRenderGraph& graph)
    {
        if (frame.graph != &graph || frame.graphEpoch != graph.ResourceEpoch())
        {
            throw std::runtime_error("METEOR Scene raster/culling requires the METEOR.Scene.Geometry declaration first.");
        }
    }

    void SceneHost::RequireCulling(const Frame& frame, const EnhancedRenderGraph& graph)
    {
        if (!frame.cullDeclared)
        {
            throw std::runtime_error("METEOR Scene raster requires the METEOR.Scene.Cull declaration first.");
        }
        if (!frame.draws.empty())
        {
            RequireGeometry(frame, graph);
        }
    }

    std::shared_ptr<const GpuGeometryVisibility::Frame> SceneHost::CameraMeshletWork() const
    {
        return frame_ ? frame_->cameraMeshlets : nullptr;
    }

    std::shared_ptr<const GpuGeometryVisibility::Frame> SceneHost::CameraDepthMeshletWork() const
    {
        return frame_ ? frame_->cameraDepthMeshlets : nullptr;
    }

    std::shared_ptr<const SceneMaterialWork> SceneHost::MaterialWork() const
    {
        return frame_ ? frame_->materialWork : nullptr;
    }

    std::shared_ptr<const SceneFrameGeometry> SceneHost::FrameGeometry() const
    {
        return frame_ ? frame_->geometryTable : nullptr;
    }

    std::shared_ptr<const SceneHybridVisibility> SceneHost::HybridVisibility() const
    {
        return frame_ ? frame_->hybridVisibility : nullptr;
    }

    void SceneHost::DeclareGeometry(EnhancedRenderGraph& graph) const
    {
        ce::profile_scope profile{ce::marker<"MaterialDeclareGeometry">()};
        const auto frame = frame_;
        if (!frame || frame->draws.empty())
        {
            return;
        }
        frame->CheckCurrent();
        if (frame->graph)
        {
            frame->CheckCurrent(&graph);
            return;
        }
        // Every world transform of the frame goes into one batched declaration.
        std::set<const MeshSurfaceBatch*> declaredGeometry;
        std::vector<const MeshSurfaceBatch*> geometry;
        for (const auto& draw : frame->draws)
        {
            declaredGeometry.insert(draw.geometry.get());
            geometry.push_back(draw.geometry.get());
            if (draw.previousGeometry && declaredGeometry.insert(draw.previousGeometry.get()).second)
            {
                geometry.push_back(draw.previousGeometry.get());
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                auto handle = graph.FindImportedTexture(texture.resource);
                if (!handle.IsValid())
                {
                    handle =
                        graph.ImportTexture(texture.resource, RHIResourceState::PixelShaderResource, "METEOR.Scene.Image");
                }
            }
        }
        for (const auto& draw : frame->shadowDraws)
        {
            if (declaredGeometry.insert(draw.geometry.get()).second)
            {
                geometry.push_back(draw.geometry.get());
            }
        }
        std::string error;
        if (!MeshSurfaceBatch::DeclareBatch(graph, geometry, error))
        {
            throw std::runtime_error(error);
        }
        frame->geometryTable->Declare(graph, frame->geometryTable);
        frame->graph = &graph;
        frame->graphEpoch = graph.ResourceEpoch();
    }

    uint32_t SceneHost::PreparedMeshletDrawCount() const
    {
        if (!frame_)
        {
            return 0;
        }
        return static_cast<uint32_t>(std::ranges::count_if(frame_->draws,
            [&](const auto& draw) { return frame_->UsesMeshlets(draw); }));
    }

    void SceneHost::EnableGeometryRouteAudit(bool enabled) const
    {
        if (frame_)
        {
            frame_->geometryRouteAuditEnabled = enabled;
        }
    }

    SceneGeometryRouteAudit SceneHost::RecordedGeometryRoutes() const
    {
        SceneGeometryRouteAudit result;
        if (frame_ && frame_->input)
        {
            result.sourceFrameId = frame_->input->View().frameId;
            result.sceneEpoch = frame_->input->View().sceneEpoch;
            result.viewId = frame_->input->View().viewId;
            result.draws = frame_->recordedGeometryRoutes;
        }
        return result;
    }

    std::array<uint32_t, 3> SceneHost::ShadowBucketSubmissionCounts() const
    {
        std::array<uint32_t, 3> result{};
        if (frame_) for (unsigned cascade = 0; cascade < 3; ++cascade)
            result[cascade] = frame_->shadowBucketSubmissions[cascade].load(std::memory_order_relaxed);
        return result;
    }

    uint32_t SceneHost::TemporalBucketSubmissionCount() const
    {
        return frame_ ? frame_->temporalBucketSubmissions.load(std::memory_order_relaxed) : 0;
    }

    uint32_t SceneHost::BlendedBucketSubmissionCount() const
    {
        return frame_ ? frame_->blendedBucketSubmissions.load(std::memory_order_relaxed) : 0;
    }

    std::array<uint32_t, 2> SceneHost::LayerBucketCounts() const
    {
        return frame_ ? std::array{frame_->indexedBucketSubmissions.load(std::memory_order_relaxed),
            frame_->indexedLogicalCommands.load(std::memory_order_relaxed)} : std::array<uint32_t, 2>{};
    }

    uint32_t SceneHost::ShadowDrawCount() const
    {
        return frame_ ? frame_->shadowDrawCount.load(std::memory_order_relaxed) : 0;
    }

    std::array<uint32_t, 3> SceneHost::RecordedShadowMeshletDrawCounts() const
    {
        std::array<uint32_t, 3> counts{};
        if (frame_)
            for (unsigned cascade = 0; cascade < counts.size(); ++cascade)
                counts[cascade] = frame_->shadowMeshletDrawCounts[cascade].load(std::memory_order_relaxed);
        return counts;
    }

    std::vector<SceneHost::ShadowMeshletOutput> SceneHost::ShadowMeshletOutputs() const
    {
        std::vector<ShadowMeshletOutput> result;
        if (frame_) for (const auto& draw : frame_->shadowDraws)
            for (unsigned cascade = 0; cascade < 3; ++cascade)
                if (draw.shadowMeshlets[cascade]) result.push_back({cascade, draw.shadowMeshletBin, draw.shadowMeshlets[cascade]});
        return result;
    }

    GpuGeometryVisibility::PreparedStats SceneHost::CameraVisibilityStats() const
    {
        return frame_ && frame_->visibility ? frame_->visibility->GetPreparedStats()
                                            : GpuGeometryVisibility::PreparedStats{};
    }

    std::array<GpuGeometryVisibility::PreparedStats, 3> SceneHost::ShadowVisibilityStats() const
    {
        std::array<GpuGeometryVisibility::PreparedStats, 3> result{};
        if (frame_ && !frame_->shadowDraws.empty())
            for (unsigned c=0;c<3;++c)
                if (const auto& list=frame_->shadowDraws.front().shadowMeshlets[c]) result[c]=list->GetPreparedStats();
        return result;
    }

    std::array<uint32_t, 3> SceneHost::ShadowCasterCounts() const
    {
        std::array<uint32_t, 3> counts{};
        if (!frame_ || !frame_->shadow)
        {
            return counts;
        }
        for (const auto& draw : frame_->shadowDraws)
        {
            if (!draw.shadowEnabled)
            {
                continue;
            }
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                counts[cascade] += draw.shadowMeshlets[cascade] ? 1u : 0u;
            }
        }
        return counts;
    }

    RGHandle SceneHost::DeclareShadow(EnhancedRenderGraph& graph, RGHandle shadowMap) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;

        ce::profile_scope profile{ce::marker<"MaterialDeclareShadow">()};
        const auto frame = frame_;
        if (!frame || !frame->shadow)
        {
            return shadowMap;
        }
        frame->CheckCurrent();
        if (frame->shadowDeclared)
        {
            throw std::runtime_error("METEOR Scene shadow requires one declaration per prepared frame.");
        }
        RequireCulling(*frame, graph);
        // One pass per cascade: identical commands, but each cascade's GPU time
        // is attributed separately by the graph profiler.
        static constexpr const char* kCascadePassNames[]{"METEOR.Scene.Shadow.C0", "METEOR.Scene.Shadow.C1",
                                                         "METEOR.Scene.Shadow.C2"};
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
            {
                shadowMap = graph.Modify(shadowMap);
            }
            std::vector<EnhancedRenderGraph::RGPassUsage> uses{{shadowMap, RHIResourceState::DepthWrite, modifyAccess}};

            for (const auto& draw : frame->shadowDraws)
            {
                if (!draw.shadowEnabled)
                {
                    continue;
                }
                if (draw.shadowMeshlets[cascade]) draw.shadowMeshlets[cascade]->AddVisibleIdsRead(graph, uses);
                uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
                if (frame->UsesMeshlets(draw))
                {
                    uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    uses.push_back(
                        {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
                }
            }
            if (frame->shadowCommands[cascade])
                frame->shadowCommands[cascade]->AddRead(graph, uses);
            NormalizeSceneReads(graph, uses);
            graph.AddPass(kCascadePassNames[cascade], uses, [frame, shadowMap, cascade](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto map = execution.ResolveHandle(shadowMap);
                const auto description = frame->device->DescribeTexture(map);
                if (description.depthOrArraySize != 3 || description.format != RHIFormat::D32Float)
                {
                    throw std::runtime_error("METEOR Scene shadow needs its three D32 cascades.");
                }
                encoder.SetViewportAndScissor(description.width, description.height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                const auto depth = RHIDepthTargetDesc::DepthSlice(map, RHIFormat::D32Float, cascade);
                const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR Scene cascade target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                const auto& batch = frame->shadowCommands[cascade];
                if (!batch) return; // No admitted shadow casters in this sealed frame.
                for (std::size_t bucket = 0; bucket < batch->buckets.size(); ++bucket)
                {
                    const auto& range = batch->buckets[bucket];
                    const auto& draw = frame->shadowDraws[range.representative];
                    encoder.SetPipeline(RHIBindPoint::Graphics,
                        frame->RasterPipeline(draw, draw.program->shadow.GetHandle()));
                    std::string error;
                    if (!RenderBindingCache::BindPass(*frame->device, encoder, RHIBindPoint::Graphics,
                        *draw.bindings, draw.program->shadowLayout, error))
                        throw std::runtime_error(error);
                    batch->Draw(encoder, bucket);
                    frame->shadowBucketSubmissions[cascade].fetch_add(1, std::memory_order_relaxed);
                    // Public counters describe logical caster commands, not CPU submissions.
                    frame->shadowMeshletDrawCounts[cascade].fetch_add(range.count, std::memory_order_relaxed);
                    frame->shadowDrawCount.fetch_add(range.count, std::memory_order_relaxed);
                }
            });
        }
        frame->shadowDeclared = true;
        return shadowMap;
    }

    std::array<RGHandle, 5> SceneHost::DeclareTemporal(EnhancedRenderGraph& graph,
        std::array<RGHandle, 5> targets) const
    {
        const auto frame = frame_;
        if (!frame || !frame->temporalCommands) return targets;
        RequireGeometry(*frame, graph);
        const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modify = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            if (versioned) targets[i] = graph.Modify(targets[i]);
            uses.push_back({targets[i], i == 4 ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget, modify});
        }
        for (const auto& draw : frame->draws)
        {
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, read});
            if (draw.previousGeometry != draw.geometry)
                uses.push_back({draw.previousGeometry->GraphOutput(graph), RHIResourceState::ShaderResource, read});
            for (const auto& texture : draw.bindings->resources.textures)
                uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, read});
        }
        frame->temporalCommands->AddRead(graph, uses);
        NormalizeSceneReads(graph, uses);
        const auto pass = graph.AddPass("Temporal.ObjectMotion", uses, [frame, targets](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 4> colors;
            for (unsigned i = 0; i < colors.size(); ++i) colors[i] = execution.ResolveHandle(targets[i]);
            const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(targets[4]), RHIFormat::D32Float);
            const auto bindings = frame->device->CreateRenderTargets(colors, &depth);
            if (!bindings.IsValid()) throw std::runtime_error("Temporal motion render targets unavailable.");
            encoder.BindRenderTargets(bindings);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            const auto& replay = frame->temporalCommands;
            for (std::size_t bucket = 0; bucket < replay->buckets.size(); ++bucket) {
                const auto& draw = frame->draws[replay->buckets[bucket].representative];
                encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->temporal[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                    throw std::runtime_error(error);
                replay->Draw(encoder, bucket);
                frame->temporalBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
            }

        }, true);
        // Sealed frame inputs only; route observations are read after the whole graph joins.
        graph.DeclareRecordingIndependent(pass);
        return targets;
    }

    SceneSurfaceTargets::Outputs SceneHost::DeclareGBuffer(EnhancedRenderGraph& graph,
                                                           const SceneSurfaceTargets::Outputs& incoming) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;

        ce::profile_scope profile{ce::marker<"MaterialDeclareGBuffer">()};
        const auto frame = frame_;
        if (!frame || frame->draws.empty())
        {
            return incoming;
        }
        frame->CheckCurrent();
        if (frame->gbufferDeclared)
        {
            throw std::runtime_error("METEOR Scene GBuffer requires one declaration per prepared frame.");
        }
        RequireCulling(*frame, graph);
        auto inputs = incoming;
        if (!frame->hybridVisibility) {
            frame->gbufferDeclared = true;
            frame->gbuffer = inputs;
            return inputs;
        }
        if (!inputs.visibility.IsValid()) throw std::runtime_error("METEOR surface requires a visibility target.");
        frame->hybridVisibility->Declare(graph, frame->hybridVisibility);
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        frame->hybridVisibility->AddRasterUses(graph, uses);
        frame->AddGeometryReads(graph, uses, false);
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
            if (frame->UsesMeshlets(draw))
            {
                uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
        NormalizeSceneReads(graph, uses);
        const auto pass = graph.AddPass("METEOR.Scene.Visibility.HW", uses, [frame, inputs](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            frame->recordedGeometryRoutes.clear();
            auto& encoder = *execution.encoder;
            if (!encoder.BeginRasterWithoutAttachments(frame->input->View().width, frame->input->View().height))
                throw std::runtime_error("METEOR HW visibility requires UAV-only raster support.");
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->RasterDrawsFor())
            {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                {
                    continue;
                }
                const bool indirect = frame->UsesVisibility(draw);
                const auto sourcePipeline = draw.program->visibilitySurface[draw.doubleSided].GetHandle();
                const auto recordedPipeline = frame->RasterPipeline(draw, sourcePipeline);
                encoder.SetPipeline(RHIBindPoint::Graphics, recordedPipeline);
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->rasterRouteRoot, frame->hybridVisibility->RouteSlice());
                if (!encoder.SetRootUnorderedBuffer(RHIBindPoint::Graphics, draw.program->visibilityKeyRoot, frame->hybridVisibility->KeySlice()))
                    throw std::runtime_error("METEOR HW visibility key binding failed.");
                frame->DrawGeometry(encoder, draw);
                if (frame->geometryRouteAuditEnabled)
                {
                    for (const auto& submitted : frame->draws)
                    {
                    if (submitted.materialBucket != draw.materialBucket || submitted.blended ||
                        (!frame->UsesMeshlets(draw) && &submitted != &draw)) continue;
                    const auto& draw = submitted;
                    const auto& source = frame->input->Draws()[draw.inputIndex];
                    const bool meshShader = frame->UsesMeshlets(draw);
                    frame->recordedGeometryRoutes.push_back({source.geometryKey,
                        source.temporalObjectId, source.temporalInstanceId,
                        sourcePipeline.id, recordedPipeline.id,
                        meshShader ? draw.geometry->MeshletCount() : 0u, meshShader, indirect});
                    }
                }
            }
        });
        // Sealed frame inputs only; route observations are read after the whole graph joins.
        graph.DeclareRecordingIndependent(pass);
        frame->hybridVisibility->DeclareSoftware(graph, frame->hybridVisibility);
        inputs = frame->hybridVisibility->DeclareResolve(graph, inputs, frame->hybridVisibility);
        if (frame->materialWork) frame->materialWork->Declare(graph, inputs.visibility, frame->materialWork);
        std::vector<EnhancedRenderGraph::RGPassUsage> restoreUses;
        for (auto* target : {&inputs.diffuse, &inputs.metalRough, &inputs.normal, &inputs.emissive, &inputs.bitmask}) {
            if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned) *target = graph.Modify(*target);
            restoreUses.push_back({*target, RHIResourceState::RenderTarget, modifyAccess});
        }
        frame->AddReconstructionReads(graph, restoreUses, inputs.visibility);
        for (const auto& draw : frame->draws) {
            if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
            restoreUses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::PixelShaderResource, readAccess});
            restoreUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::PixelShaderResource, readAccess});
            for (const auto& texture : draw.bindings->resources.textures)
                restoreUses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
        }
        NormalizeSceneReads(graph, restoreUses);
        graph.AddPass("METEOR.Scene.Material.Resolve", restoreUses, [frame, inputs](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < colors.size(); ++i) colors[i] = execution.ResolveHandle(handles[i]);
            const auto targets = frame->device->CreateRenderTargets(colors, nullptr);
            if (!targets.IsValid()) throw std::runtime_error("METEOR reconstructed surface target binding failed.");
            encoder.BindRenderTargets(targets);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            for (const auto& draw : frame->RasterDrawsFor()) {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended) continue;
                frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->gbuffer[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error)) throw std::runtime_error(error);
                frame->DrawReconstruction(encoder, draw);
            }
        });
        frame->gbufferDeclared = true;
        frame->gbuffer = inputs;
        return inputs;
    }

    void SceneHost::DeclareHistory(EnhancedRenderGraph& graph, RGHandle depth) const
    {
        if (!frame_ || !frame_->nextCameraHistory) return;
        frame_->CheckCurrent(&graph);
        if (!frame_->gbufferDeclared || depth.index != frame_->gbuffer.depth.index)
            throw std::runtime_error("METEOR history requires its final surface depth.");
        frame_->nextCameraHistory->Declare(graph, depth);
    }

    void SceneHost::DeclareDecalInputs(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                       const std::array<RGHandle, 3>& baseline) const
    {
        const auto frame = frame_;
        if (!frame || frame->draws.empty())
        {
            return;
        }
        frame->CheckCurrent(&graph);
        const auto expected = Colors(frame->gbuffer), actual = Colors(inputs);
        if (!frame->gbufferDeclared || frame->colorDeclared || frame->decalDeclared ||
            !std::equal(expected.begin(), expected.end(), actual.begin(),
                        [](RGHandle a, RGHandle b) { return a.index == b.index; }) ||
            frame->gbuffer.depth.index != inputs.depth.index ||
            std::ranges::any_of(baseline, [](RGHandle handle) { return !handle.IsValid(); }))
        {
            throw std::runtime_error(
                "METEOR Scene Decal requires its GBuffer, a valid snapshot and one declaration before Color.");
        }
        frame->decalBaseline = baseline;
        frame->gbuffer = inputs;
        frame->decalDeclared = true;
    }

    RGHandle SceneHost::DeclareHdrTarget(EnhancedRenderGraph& graph, const EnhancedFrameContext& context)
    {
        if (!context.resources || !context.width || !context.height)
        {
            throw std::runtime_error("METEOR HDR target requires a device and nonzero view dimensions.");
        }
        RGTextureDesc description{};
        description.width = context.width;
        description.height = context.height;
        description.format = RHIFormat::RGBA16Float;
        description.allowRenderTarget = true;
        description.name = "METEOR.Scene.HdrColor";
        auto output = graph.CreateTexture(description);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            output = graph.Write(output);
        }
        const auto access = graph.GetSchedulingMode() == RGSchedulingMode::DeclarationOrder
            ? RGAccessMode::LegacyState : RGAccessMode::Write;
        graph.AddPass("METEOR.Scene.HdrClear", {{output, RHIResourceState::RenderTarget, access}},
            [device = context.resources, output](const auto& execution) {
                const RHITextureHandle color = execution.ResolveHandle(output);
                const auto targets = device->CreateRenderTargets({&color, 1});
                if (!targets.IsValid())
                {
                    throw std::runtime_error("METEOR HDR target binding failed.");
                }
                execution.encoder->BindRenderTargets(targets);
                const float clear[]{0.f, 0.f, 0.f, 0.f};
                execution.encoder->ClearRenderTargets(targets, clear);
            });
        return output;
    }

    RGHandle SceneHost::DeclareColor(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                     RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap) const
    {

        const auto frame = frame_;
        if (!frame || frame->draws.empty())
        {
            return lighting;
        }
        frame->CheckCurrent();
        const auto expected = Colors(frame->gbuffer), actual = Colors(inputs);
        const bool sameTargets = std::equal(expected.begin(), expected.end(), actual.begin(),
                                            [](RGHandle a, RGHandle b) { return a.index == b.index; });
        if (!frame->gbufferDeclared || frame->graph != &graph || frame->graphEpoch != graph.ResourceEpoch() ||
            frame->colorDeclared || !sameTargets || frame->gbuffer.depth.index != inputs.depth.index)
        {
            throw std::runtime_error("METEOR Scene color requires its own GBuffer producer and one declaration.");
        }
        frame->lookup->DeclareInputs(graph);
        if (frame->runtimeEffects)
        {
            frame->runtimeEffects->DeclareInputs(graph);
        }
        if (frame->volume)
        {
            frame->volume->DeclareCoefficients(graph, frame->volumeBindings);
        }
        if (frame->subsurface)
        {
            frame->subsurface->DeclareInputs(graph);
        }
        lighting = DeclareShading(graph, inputs, lighting, ambientOcclusion, shadowMap, false);
        if (frame->refraction)
        {
            frame->refraction->DeclareInputs(graph);
        }
        frame->colorDeclared = true;
        frame->lookup->DeclareReady(graph);
        return lighting;
    }

    std::vector<SceneHost::ForwardDraw> SceneHost::BlendedDraws() const
    {
        std::vector<ForwardDraw> result;
        if (!frame_)
        {
            return result;
        }
        for (std::size_t i = 0; i < frame_->input->Draws().size(); ++i)
        {
            const auto& draw = frame_->input->Draws()[i];
            const auto& p = draw.material->generation->cooked.product.program;
            if (p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u) != 0))
            {
                result.push_back({i, draw.geometryKey, draw.viewDepth});
            }
        }
        return result;
    }

    RGHandle SceneHost::DeclareBlended(EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices, RGHandle lighting,
                                       RGHandle shadowMap, const EnhancedForwardLighting& forward) const
    {

        const auto frame = frame_;
        if (indices.empty()) throw std::runtime_error("METEOR blended range is empty.");
        const auto index = indices.front();
        if (!frame || !frame->colorDeclared || !frame->alphaLookup || !lighting.IsValid() ||
            index >= frame->input->Draws().size() || !forward.lights.IsValid() || !forward.counts.IsValid() ||
            !forward.indices.IsValid() || !forward.graphCounts.IsValid() || !forward.graphIndices.IsValid())
        {
            throw std::runtime_error("Graph Blend needs its exact prepared frame and light-tile resources.");
        }
        frame->CheckCurrent(&graph);
        for (const auto input : indices) if (input >= frame->input->Draws().size() || !frame->alphaDeclared.insert(input).second)
        {
            throw std::runtime_error("Graph Blend draw declared twice.");
        }
        if (!frame->alphaInputsDeclared)
        {
            frame->alphaLookup->DeclareInputs(graph);
            frame->alphaInputsDeclared = true;
        }
        const auto& p = frame->input->Draws()[index].material->generation->cooked.product.program;
        const bool needsSurfaceBuffer = (p.features & 0x1800u) != 0;
        if (needsSurfaceBuffer)
        {
            DeclareForwardGBuffer(graph, frame->gbuffer, index);
        }
        if ((p.features & 0x0800u) != 0)
        {
            if (frame->runtimeEffects)
            {
                frame->runtimeEffects->DeclareBackground(graph, lighting, frame->gbuffer.depth);
            }
            else
            {
                frame->refraction->DeclareBackground(graph, lighting, frame->gbuffer.depth);
            }
        }
        lighting = DeclareShading(graph, needsSurfaceBuffer ? frame->forwardGbuffer : frame->gbuffer, lighting, {},
                                  shadowMap, (p.features & 0x0800u) != 0, index, forward, indices);
        frame->alphaLookup->DeclareReady(graph);
        return lighting;
    }

    RGHandle SceneHost::DeclareBlendedStream(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle shadowMap,
                                             const EnhancedForwardLighting& tiles) const
    {
        const auto draws = BlendedDraws();
        if (draws.empty())
        {
            return lighting;
        }
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitSingleWriter)
        {
            throw std::runtime_error("METEOR blended stream requires ExplicitVersioned resource declarations.");
        }
        ce::profile_scope profile{ce::marker<"MaterialDeclareBlended">()};
        // Media inside blended shading use the frame's volume coefficients. The
        // composite itself is METEOR.Scene.Volume, declared once before this stream.
        EnhancedForwardLighting shared = tiles;
        if (const auto volume = VolumeFrame())
        {
            shared.volumeConstants = volume->TransportConstants();
            shared.volumeTriangles = volume->Triangles();
            shared.volumeCoefficients = RHIBufferSlice::Whole(volume->Coefficients());
            shared.graphVolumeCoefficients = volume->GraphCoefficients(graph);
            shared.volumeEnvironment = volume->LightingBindings({})[1].resource;
        }
        else
        {
            shared.volumeConstants = frame_->emptyMediumConstants;
            shared.volumeTriangles = frame_->emptyMediumTriangles;
            shared.volumeCoefficients = shared.volumeTriangles;
        }
        for (const auto& range : frame_->blendedRanges)
            lighting = DeclareBlended(graph, range.inputs, lighting, shadowMap, shared);
        return lighting;
    }

    RGHandle SceneHost::DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                      RGHandle shadowMap) const
    {
        const auto frame = frame_;
        if (!frame || !frame->volume || frame->volumeDeclared)
        {
            return lighting;
        }
        frame->CheckCurrent(&graph);
        if (!frame->colorDeclared)
        {
            throw std::runtime_error("METEOR Scene Volume requires completed surface shading.");
        }
        const auto result = frame->volume->DeclareComposite(graph, lighting, depth, shadowMap);
        frame->volumeDeclared = true;
        return result;
    }
    void SceneHost::DeclareForwardGBuffer(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& opaque,
                                          std::size_t index) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;

        const auto frame = frame_;
        if (!frame->forwardGbuffer.depth.IsValid())
        {
            RGTextureDesc desc;
            desc.width = frame->input->View().width;
            desc.height = frame->input->View().height;
            desc.allowRenderTarget = true;
            auto targets = std::array{&frame->forwardGbuffer.diffuse, &frame->forwardGbuffer.metalRough,
                                      &frame->forwardGbuffer.normal, &frame->forwardGbuffer.emissive,
                                      &frame->forwardGbuffer.bitmask};
            for (unsigned i = 0; i < targets.size(); ++i)
            {
                desc.format = SceneSurfaceTargets::GetRenderTargetFormat(i);
                desc.name = "METEOR.Blended.Surface";
                *targets[i] = graph.CreateTexture(desc);
            }
            desc.format = RHIFormat::D32Float;
            desc.allowRenderTarget = false;
            desc.allowDepthStencil = true;
            desc.name = "METEOR.Blended.Depth";
            frame->forwardGbuffer.depth = graph.CreateTexture(desc);
        }
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            frame->forwardGbuffer.depth = graph.Write(frame->forwardGbuffer.depth);
        }
        const auto copiedDepth = frame->forwardGbuffer.depth;
        graph.AddPass("METEOR.Blended.DepthCopy",
                      {{opaque.depth, RHIResourceState::CopySource, readAccess},
                       {copiedDepth, RHIResourceState::CopyDest, writeAccess}},
                      [frame, opaque, copiedDepth](const auto& execution) {
                          frame->CheckCurrent(execution.graph);
                          execution.encoder->CopyResource(execution.ResolveHandle(copiedDepth),
                                                          execution.ResolveHandle(opaque.depth));
                      });
        frame->forwardGbuffer = AdvanceSceneSurface(graph, frame->forwardGbuffer, RGAccessMode::Write);
        const auto inputs = frame->forwardGbuffer;
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{{inputs.depth, RHIResourceState::DepthWrite, modifyAccess}};
        for (const auto target : Colors(inputs))
        {
            uses.push_back({target, RHIResourceState::RenderTarget, writeAccess});
        }
        for (const auto& draw : frame->draws)
        {
            if (draw.inputIndex != index || !draw.program->hasSurface)
            {
                continue;
            }
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
            if (frame->UsesMeshlets(draw))
            {
                uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
        frame->AddGeometryReads(graph, uses, true);
        NormalizeSceneReads(graph, uses);
        graph.AddPass("METEOR.Blended.Surface", uses, [frame, inputs, index](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < colors.size(); ++i)
            {
                colors[i] = execution.ResolveHandle(handles[i]);
            }
            RHIRenderTargetBinding targets;
            {
                std::lock_guard lock(frame->forwardTargetMutex);
                if (!frame->forwardTargets.IsValid())
                {
                    const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    frame->forwardTargets = frame->device->CreateRenderTargets(colors, &depth);
                }
                targets = frame->forwardTargets;
            }
            if (!targets.IsValid())
            {
                throw std::runtime_error("METEOR Scene transmission GBuffer binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->RasterDrawsFor())
            {
                if (draw.inputIndex != index || !draw.program->hasSurface)
                {
                    continue;
                }
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->gbuffer[draw.doubleSided].GetHandle()));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                frame->DrawGeometry(encoder, draw);
            }
        });
    }

    void SceneHost::DeclareRefractionCapture(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                             std::optional<std::size_t> index) const
    {
        const auto frame = frame_;
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        const auto targets = frame->refraction->DeclareCaptureOutputs(graph);
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {inputs.depth, RHIResourceState::DepthRead, readAccess},
            {inputs.bitmask, RHIResourceState::ShaderResource, readAccess}};
        for (const auto input : targets)
        {
            uses.push_back({input, RHIResourceState::RenderTarget, writeAccess});
        }
        for (const auto& draw : frame->draws)
        {
            if (!draw.program->hasTransmission || (index && draw.inputIndex != *index))
            {
                continue;
            }
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
            if (frame->UsesMeshlets(draw))
            {
                uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
        frame->AddGeometryReads(graph, uses, index.has_value());
        NormalizeSceneReads(graph, uses);
        graph.AddPass("METEOR.Scene.RefractionCapture", uses, [frame, inputs, index](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            const auto depth =
                RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
            const auto targets = frame->device->CreateRenderTargets(frame->refraction->Inputs(), &depth);
            const RHIBindingDesc descriptions[]{
                RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
            const auto table = frame->device->CreateBindings(descriptions);
            const auto decalTable = frame->DecalTable(execution, true);
            if (!targets.IsValid() || !table.IsValid())
            {
                throw std::runtime_error("METEOR Scene refraction capture binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->RasterDrawsFor())
            {
                if (!draw.program->hasTransmission || (index && draw.inputIndex != *index))
                {
                    continue;
                }
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->refraction[draw.doubleSided].GetHandle()));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                frame->BindDecal(encoder, draw, decalTable, true);
                frame->DrawGeometry(encoder, draw);
            }
        });
    }

    RGHandle SceneHost::DeclareShading(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                       RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                       bool transmissionStage, std::optional<std::size_t> blended,
                                       EnhancedForwardLighting forward, std::vector<std::size_t> blendedGroup) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;

        const auto frame = frame_;
        const bool reconstruct = !blended && !transmissionStage;
        const auto lookup = blended ? frame->alphaLookup : frame->lookup;
        const auto selected = [blended, blendedGroup](const Frame::Draw& draw) {
            return blended ? draw.blended && (blendedGroup.empty() ? draw.inputIndex == *blended :
                std::ranges::find(blendedGroup, draw.inputIndex) != blendedGroup.end()) : !draw.blended;
        };
        const bool ordinary = !frame->runtimeEffects || std::ranges::any_of(frame->draws, [&](const auto& draw) {
            return selected(draw) && draw.program->hasSurface && !draw.program->hasSpecial &&
                   draw.program->hasTransmission == transmissionStage;
        });
        std::shared_ptr<const IndirectMaterialBatch> blendedCommands;
        if (blended && ordinary) {
            const auto order = blendedGroup.empty() ? std::vector<size_t>{*blended} : blendedGroup;
            const auto found = std::ranges::find_if(frame->blendedRanges,
                [&](const auto& range) { return range.inputs == order; });
            if (found == frame->blendedRanges.end())
                throw std::runtime_error("METEOR blended range was not prepared.");
            blendedCommands = found->commands[forward.reference ? 1 : 0];
        }
        if (ordinary)
        {
            lighting = AdvanceSceneColor(graph, lighting);
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {lighting, RHIResourceState::RenderTarget, modifyAccess},
            {inputs.depth, RHIResourceState::DepthRead, readAccess},
            {inputs.bitmask, RHIResourceState::ShaderResource, readAccess},
            {ambientOcclusion, RHIResourceState::ShaderResource, readAccess}};
        if (reconstruct) std::erase_if(uses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
        if (blended)
        {
            uses.pop_back();
        }
        frame->AddDecalUses(uses, transmissionStage || blended.has_value(), readAccess);
        lookup->DeclareShadingInputs(graph, uses);
        if (blended)
        {
            uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, readAccess});
            uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, readAccess});
            if (frame->volume)
            {
                uses.push_back(
                    {frame->volume->GraphCoefficients(graph), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
        if (frame->environment.IsValid())
        {
            auto environment = graph.FindImportedTexture(frame->environment);
            if (!environment.IsValid())
            {
                environment =
                    graph.ImportTexture(frame->environment, RHIResourceState::PixelShaderResource, "METEOR.Scene.HDR");
            }
            uses.push_back({environment, RHIResourceState::PixelShaderResource, readAccess});
        }
        if (frame->shadow)
        {
            uses.push_back({shadowMap, RHIResourceState::ShaderResource, readAccess});
        }
        for (const auto& draw : frame->draws)
        {
            if (!selected(draw))
            {
                continue;
            }
            uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
            if (frame->UsesMeshlets(draw))
            {
                uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto& texture : draw.bindings->resources.textures)
            {
                uses.push_back(
                    {graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, readAccess});
            }
        }
        const bool explicitLookup = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto lookupRead = explicitLookup ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto lookupWrite = explicitLookup ? RGAccessMode::Write : RGAccessMode::LegacyState;
        for (unsigned part = 0; part < (lookup->RequiresCapture() ? 2u : 0u); ++part)
        {
            std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
                {inputs.depth, RHIResourceState::DepthRead, lookupRead},
                {inputs.bitmask, RHIResourceState::ShaderResource, lookupRead}};
            if (reconstruct) std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
            frame->AddDecalUses(captureUses, transmissionStage || blended.has_value(), lookupRead);
            const unsigned first = part ? 8 : 0, count = part ? 3 : 8;
            const auto lookupInputs = lookup->DeclareCaptureOutputs(graph, first, count);
            for (unsigned i = first; i < first + count; ++i)
            {
                captureUses.push_back({lookupInputs[i], RHIResourceState::RenderTarget, lookupWrite});
            }
            for (const auto& draw : frame->draws)
            {
                if (!selected(draw))
                {
                    continue;
                }
                captureUses.push_back(
                    {draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, lookupRead});
                if (frame->UsesMeshlets(draw))
                {
                    captureUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, lookupRead});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    captureUses.push_back({graph.FindImportedTexture(texture.resource),
                                           RHIResourceState::PixelShaderResource, lookupRead});
                }
            }
            if (reconstruct) frame->AddReconstructionReads(graph, captureUses, inputs.visibility);
            else frame->AddGeometryReads(graph, captureUses, blended.has_value());
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                reconstruct ? "METEOR.Scene.Lookup.Resolve" : "METEOR.Scene.LookupCapture", captureUses,
                [frame, inputs, first, count, part, transmissionStage, blended, lookup, selected,
                 forward, reconstruct](const auto& execution) {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto handles = lookup->Inputs().subspan(first, count);
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto targets = frame->device->CreateRenderTargets(handles, reconstruct ? nullptr : &depth);
                    const RHIBindingDesc descriptions[]{
                        RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull(),
                        RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                        RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                        RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull()};
                    const auto table = frame->device->CreateBindings(descriptions);
                    const auto decalTable = frame->DecalTable(execution, transmissionStage || blended.has_value());
                    frame->CheckCurrent();
                    if (!targets.IsValid() || !table.IsValid())
                    {
                        throw std::runtime_error("METEOR Scene lookup capture binding failed.");
                    }
                    encoder.BindRenderTargets(targets);
                    const float clear[]{0, 0, 0, 0};
                    encoder.ClearRenderTargets(targets, clear);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->RasterDrawsFor(blended))
                    {
                        if (!draw.program->hasSurface || !selected(draw))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedLookup : draw.program->lookup)[part][draw.doubleSided]
                                .GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->lookup[part][draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->BindForward(encoder, draw, forward);
                        if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
        }
        lookup->DeclareBake(graph, inputs.bitmask);
        uses.push_back({lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, readAccess});
        if (transmissionStage && !frame->runtimeEffects)
        {
            DeclareRefractionCapture(graph, inputs, blended);
            frame->refraction->DeclareBake(graph, *lookup, inputs.bitmask, shadowMap);
            uses.push_back({frame->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, readAccess});
        }
        const bool special = std::ranges::any_of(frame->draws, [&](const auto& draw) {
            return selected(draw) && draw.program->hasSpecial && draw.program->hasTransmission == transmissionStage;
        });
        if (frame->subsurface && special)
        {
            const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
            const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
            const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
            const auto targets = frame->subsurface->DeclareCaptureOutputs(graph);
            frame->subsurface->DeclareReflection(graph, *lookup, inputs.bitmask);
            std::vector<EnhancedRenderGraph::RGPassUsage> captureUses{
                {inputs.depth, RHIResourceState::DepthRead, readAccess},
                {inputs.bitmask, RHIResourceState::ShaderResource, readAccess},
                {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, readAccess},
                {frame->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, readAccess}};
            if (reconstruct) std::erase_if(captureUses, [&](const auto& use) { return use.handle.index == inputs.depth.index; });
            frame->AddDecalUses(captureUses, transmissionStage || blended.has_value(), readAccess);
            lookup->DeclareShadingInputs(graph, captureUses);
            if (blended)
            {
                captureUses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, readAccess});
                captureUses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto target : targets)
            {
                captureUses.push_back({target, RHIResourceState::RenderTarget, writeAccess});
            }
            if (frame->shadow)
            {
                captureUses.push_back({shadowMap, RHIResourceState::ShaderResource, readAccess});
            }
            for (const auto& draw : frame->draws)
            {
                if (!draw.program->hasSubsurface)
                {
                    continue;
                }
                captureUses.push_back(
                    {draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, readAccess});
                if (frame->UsesMeshlets(draw))
                {
                    captureUses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, readAccess});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    captureUses.push_back({graph.FindImportedTexture(texture.resource),
                                           RHIResourceState::PixelShaderResource, readAccess});
                }
            }
            if (transmissionStage)
            {
                captureUses.push_back(
                    {frame->refraction->GraphSamples(graph), RHIResourceState::PixelShaderResource, readAccess});
            }
            if (reconstruct) frame->AddReconstructionReads(graph, captureUses, inputs.visibility);
            else frame->AddGeometryReads(graph, captureUses, blended.has_value());
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                "METEOR.Scene.SubsurfaceCapture", captureUses,
                [frame, inputs, shadowMap, transmissionStage, blended, lookup, selected,
                 forward, reconstruct](const auto& execution) {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto targets = frame->device->CreateRenderTargets(frame->subsurface->Inputs(), reconstruct ? nullptr : &depth);
                    const RHIBindingDesc descriptions[]{
                        RHIBindingDesc::SrvCube(lookup->UsesRuntimeEvaluation() ? frame->environment : RHITextureHandle{},
                                                frame->environment.IsValid()
                                                    ? frame->device->DescribeTexture(frame->environment).format
                                                    : RHIFormat::RGBA16Float, 1).OrNull(),
                        RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                        RHIBindingDesc::Srv2D({}, RHIFormat::RG16Float).OrNull(),
                        RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap)
                                                               : RHITextureHandle{},
                                                 RHIFormat::R32Float, 3)
                            .OrNull()};
                    const auto table = frame->device->CreateBindings(descriptions);
                    const auto decalTable = frame->DecalTable(execution, transmissionStage || blended.has_value());
                    if (!targets.IsValid() || !table.IsValid())
                    {
                        throw std::runtime_error("METEOR Scene SSS capture target binding failed.");
                    }
                    encoder.BindRenderTargets(targets);
                    const float clear[]{0, 0, 0, 0};
                    encoder.ClearRenderTargets(targets, clear);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->RasterDrawsFor(blended))
                    {
                        if (!selected(draw) || !draw.program->hasSubsurface ||
                            (draw.program->hasTransmission && !transmissionStage))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->subsurface[draw.doubleSided].GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->subsurface[draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->BindForward(encoder, draw, forward);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                              RHIBufferSlice::Whole(frame->subsurface->Reflection()));
                        if (draw.program->hasTransmission)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                                  RHIBufferSlice::Whole(frame->refraction->Samples()));
                        }
                        if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
            frame->subsurface->DeclareFilter(graph, inputs.bitmask);
            uses.push_back(
                {frame->subsurface->GraphReflection(graph), RHIResourceState::PixelShaderResource, readAccess});
            uses.push_back(
                {frame->subsurface->GraphIrradiance(graph), RHIResourceState::PixelShaderResource, readAccess});
        }
        if (ordinary)
        {
            if (blendedCommands) frame->AddVisibilityReads(graph, uses);
            else if (reconstruct) frame->AddReconstructionReads(graph, uses, inputs.visibility);
            else frame->AddGeometryReads(graph, uses, blended.has_value());
            if (blendedCommands) blendedCommands->AddRead(graph, uses);
            NormalizeSceneReads(graph, uses);
            const auto colorPass = graph.AddPass(
                blended             ? "METEOR.Blended.Color"
                : transmissionStage ? "METEOR.Scene.TransmissionColor"
                                    : "METEOR.Scene.Color",
                uses,
                [frame, inputs, lighting, ambientOcclusion, shadowMap, transmissionStage, blended, lookup, selected,
                 forward, blendedCommands, reconstruct](const auto& execution) mutable {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto color = execution.ResolveHandle(lighting);
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto target = frame->device->CreateRenderTargets({&color, 1}, reconstruct ? nullptr : &depth);
                    std::array<RHIBindingDesc, 4> descriptions{
                        RHIBindingDesc::SrvCube(frame->environment,
                                                frame->environment.IsValid()
                                                    ? frame->device->DescribeTexture(frame->environment).format
                                                    : RHIFormat::RGBA16Float,
                                                1)
                            .OrNull(),
                        RHIBindingDesc::Srv2D(execution.ResolveHandle(inputs.bitmask), RHIFormat::R32Uint),
                        RHIBindingDesc::Srv2D(blended ? RHITextureHandle{} : execution.ResolveHandle(ambientOcclusion),
                                              RHIFormat::RG16Float)
                            .OrNull(),
                        RHIBindingDesc::SrvArray(frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{},
                                                 RHIFormat::R32Float, 3)
                            .OrNull()};
                    const auto table = frame->device->CreateBindings(descriptions);
                    const auto decalTable = frame->DecalTable(execution, transmissionStage || blended.has_value());
                    if (frame->volume && blended)
                    {
                        forward.volumeTable = frame->device->CreateBindings(frame->volume->LightingBindings(
                            frame->shadow ? execution.ResolveHandle(shadowMap) : RHITextureHandle{}));
                        if (!forward.volumeTable.IsValid())
                        {
                            throw std::runtime_error("Graph medium lighting binding failed.");
                        }
                    }
                    frame->CheckCurrent();
                    if (!target.IsValid() || !table.IsValid())
                    {
                        throw std::runtime_error("METEOR Scene color/depth/AO/environment binding failed.");
                    }
                    encoder.BindRenderTargets(target);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->RasterDrawsFor({}, blended.has_value()))
                    {
                        if (!selected(draw) || !draw.program->hasSurface ||
                            draw.program->hasTransmission != transmissionStage ||
                            (frame->runtimeEffects && draw.program->hasSpecial))
                        {
                            continue;
                        }
                        if (blendedCommands && (blendedCommands->buckets.empty() ||
                            &draw != &frame->draws[blendedCommands->buckets.front().representative])) continue;
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle()));
                        if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, draw.program->color[draw.doubleSided].GetHandle());
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants : draw.constants);
                        if (!reconstruct) frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->BindForward(encoder, draw, forward);
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                        if (draw.program->hasSpecial)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 4,
                                                  RHIBufferSlice::Whole(frame->subsurface->Reflection()));
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 5,
                                                  RHIBufferSlice::Whole(frame->subsurface->Irradiance()));
                        }
                        if (draw.program->hasTransmission)
                        {
                            encoder.SetRootBuffer(RHIBindPoint::Graphics, 6,
                                                  RHIBufferSlice::Whole(frame->refraction->Samples()));
                        }
                        if (blendedCommands) {
                            blendedCommands->Draw(encoder, 0);
                            frame->blendedBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
                        } else if (reconstruct) frame->DrawReconstruction(encoder, draw);
                        else frame->DrawGeometry(encoder, draw);
                    }
                });
            if (!blended && !transmissionStage)
            {
                // Prepared opaque shading does not consume another callback CPU output.
                graph.DeclareRecordingIndependent(colorPass);
            }
        }
        if (frame->runtimeEffects && special)
        {
            lighting = DeclareRuntimeEffects(graph, inputs, lighting, ambientOcclusion, shadowMap,
                                             transmissionStage, blended, forward);
        }
        return lighting;
    }

    RGHandle SceneHost::DeclareRuntimeEffects(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                              RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                              bool transmissionStage, std::optional<std::size_t> blended,
                                              EnhancedForwardLighting forward) const
    {
        const auto frame = frame_;
        const bool reconstruct = !blended && !transmissionStage;
        const auto effects = frame->runtimeEffects;
        const auto lookup = blended ? frame->alphaLookup : frame->lookup;
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modify = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;
        const auto selected = [blended, transmissionStage](const Frame::Draw& draw) {
            return draw.program->hasSurface && draw.program->hasSpecial &&
                   draw.program->hasTransmission == transmissionStage &&
                   (blended ? draw.blended && draw.inputIndex == *blended : !draw.blended);
        };
        const auto commonUses = [&] {
            std::vector<EnhancedRenderGraph::RGPassUsage> uses{
                {inputs.bitmask, RHIResourceState::ShaderResource, read},
                {lookup->GraphSamples(graph), RHIResourceState::PixelShaderResource, read}};
            if (!blended)
            {
                uses.push_back({ambientOcclusion, RHIResourceState::ShaderResource, read});
            }
            frame->AddDecalUses(uses, transmissionStage || blended.has_value(), read);
            lookup->DeclareShadingInputs(graph, uses);
            if (blended)
            {
                uses.push_back({forward.graphCounts, RHIResourceState::ShaderResource, read});
                uses.push_back({forward.graphIndices, RHIResourceState::ShaderResource, read});
                if (frame->volume)
                {
                    uses.push_back({frame->volume->GraphCoefficients(graph), RHIResourceState::PixelShaderResource, read});
                }
            }
            if (frame->shadow)
            {
                uses.push_back({shadowMap, RHIResourceState::ShaderResource, read});
            }
            for (const auto& draw : frame->DrawsFor(blended))
            {
                if (!selected(draw))
                {
                    continue;
                }
                uses.push_back({draw.geometry->GraphOutput(graph), RHIResourceState::ShaderResource, read});
                if (frame->UsesMeshlets(draw))
                {
                    uses.push_back({draw.geometry->GraphMeshlets(graph), RHIResourceState::ShaderResource, read});
                }
                for (const auto& texture : draw.bindings->resources.textures)
                {
                    uses.push_back({graph.FindImportedTexture(texture.resource), RHIResourceState::PixelShaderResource, read});
                }
            }
            if (reconstruct) frame->AddReconstructionReads(graph, uses, inputs.visibility);
            else frame->AddGeometryReads(graph, uses, blended.has_value());
            return uses;
        };
        const auto raster = [frame, effects, lookup, inputs, ambientOcclusion, shadowMap, transmissionStage,
                             blended, selected, reconstruct](const EnhancedRenderGraph::ExecuteContext& execution,
                                                std::size_t tileIndex, int part, RGHandle lighting,
                                                EnhancedForwardLighting forward) {
            frame->CheckCurrent(execution.graph);
            effects->CheckCurrent(*execution.graph);
            const auto& tile = effects->Tiles()[tileIndex];
            const bool capture = part >= 0;
            auto& encoder = *execution.encoder;
            const auto bindings = frame->RuntimeTables(execution, inputs, lighting, ambientOcclusion, shadowMap,
                                                       blended.has_value(), transmissionStage);
            const auto targets = capture ? effects->CaptureTargets(static_cast<unsigned>(part)) : bindings.color;
            forward.volumeTable = bindings.volume;
            encoder.BindRenderTargets(targets);
            if (capture)
            {
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(targets, clear);
                if (!encoder.SetViewport(-static_cast<float>(tile.sourceX), -static_cast<float>(tile.sourceY),
                                         frame->input->View().width, frame->input->View().height) ||
                    !encoder.SetScissor(0, 0, tile.sourceWidth, tile.sourceHeight))
                {
                    throw std::runtime_error("Runtime special MRT tile needs a supported offset viewport.");
                }
            }
            else
            {
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                if (!encoder.SetScissor(tile.x, tile.y, tile.width, tile.height))
                {
                    throw std::runtime_error("Runtime special tile needs a supported bounded scissor.");
                }
            }
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->RasterDrawsFor(blended))
            {
                if (!selected(draw))
                {
                    continue;
                }
                const auto pipeline = capture ? draw.program->runtimeEffects[part][draw.doubleSided].GetHandle()
                    : (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle();
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, pipeline));
                if (reconstruct) frame->BindReconstruction(execution, draw, inputs.visibility, pipeline);
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                          blended && forward.reference ? draw.referenceConstants : draw.constants);
                if (!reconstruct) frame->BindGeometry(encoder, draw);
                encoder.SetBindings(RHIBindPoint::Graphics, 2, bindings.common);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(lookup->Samples()));
                frame->BindDecal(encoder, draw, bindings.decal, transmissionStage || blended.has_value());
                frame->BindForward(encoder, draw, forward);
                const unsigned firstRoot = 16 + 2 + (draw.program->hasTransmission ? 1 : 0);
                effects->Bind(encoder, firstRoot, tileIndex);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, RHIBufferSlice::Whole(lookup->Samples()));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 5, RHIBufferSlice::Whole(lookup->Samples()));
                if (draw.program->hasTransmission)
                {
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 6, RHIBufferSlice::Whole(lookup->Samples()));
                }
                if (reconstruct) frame->DrawReconstruction(encoder, draw);
                else frame->DrawGeometry(encoder, draw);
            }
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
        };
        if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
        {
            throw std::runtime_error("Runtime special tiling requires graph-owned repeated phase scheduling.");
        }
        const auto tileDepth = effects->DeclareDepthOutput(graph);
        const auto firstTargets = effects->DeclareCaptureOutputs(graph, 0);
        const auto targets = effects->DeclareCaptureOutputs(graph, 1);
        lighting = AdvanceSceneColor(graph, lighting);
        auto external = commonUses();
        external.push_back({inputs.depth, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
        external.push_back({tileDepth, RHIResourceState::DepthWrite, RGAccessMode::Write});
        for (unsigned i = 0; i < targets.size(); ++i)
        {
            external.push_back({i < 8 ? firstTargets[i] : targets[i], RHIResourceState::RenderTarget, RGAccessMode::Write});
        }
        external.push_back({lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
        effects->DeclareShadingInputs(graph, external, false, transmissionStage);
        // Tile depth is already the external Write. Its local Read belongs to the
        // capture phases, not a duplicate access on the outer version.
        std::erase_if(external, [&](const auto& use) {
            return use.handle.index == tileDepth.index && use.access == RGAccessMode::Read;
        });
        NormalizeSceneReads(graph, external);
        std::vector<EnhancedRenderGraph::RepeatedPhase> phases;
        phases.push_back({"METEOR.Scene.RuntimeTileDepth",
                          {{inputs.depth, RHIResourceState::PixelShaderResource, RGAccessMode::Read},
                           {tileDepth, RHIResourceState::DepthWrite, RGAccessMode::Write}}});
        for (unsigned part = 0; part < 2; ++part)
        {
            auto captureUses = commonUses();
            effects->DeclareShadingInputs(graph, captureUses, false, transmissionStage);
            const unsigned first = part ? 8 : 0, count = part ? 6 : 8;
            for (unsigned i = first; i < first + count; ++i)
            {
                captureUses.push_back({i < 8 ? firstTargets[i] : targets[i], RHIResourceState::RenderTarget,
                                       RGAccessMode::Write});
            }
            NormalizeSceneReads(graph, captureUses);
            phases.push_back({part ? "METEOR.Scene.RuntimeEffectCapture1" : "METEOR.Scene.RuntimeEffectCapture0",
                              std::move(captureUses)});
        }
        auto colorUses = commonUses();
        colorUses.push_back({inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read});
        colorUses.push_back({lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
        effects->DeclareShadingInputs(graph, colorUses);
        NormalizeSceneReads(graph, colorUses);
        phases.push_back({"METEOR.Scene.RuntimeEffectColor", std::move(colorUses)});
        graph.AddRepeatedPass("METEOR.Scene.RuntimeEffects", external, phases,
            static_cast<std::uint32_t>(effects->Tiles().size()),
            [effects, raster, inputs, lighting, forward](const auto& execution, std::uint32_t iteration,
                                                         std::uint32_t phase) {
                if (phase == 0)
                {
                    effects->RecordDepth(execution, inputs.depth, iteration);
                }
                else
                {
                    raster(execution, iteration, phase == 3 ? -1 : static_cast<int>(phase - 1), lighting, forward);
                }
            });
        return lighting;
    }

    bool SceneHost::PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                                          RHISubmissionTicket ticket)
    {
        if (!HasDraws())
        {
            error.clear();
            return true;
        }
        {
            std::lock_guard lock(recordingMutex_);
            const auto found = recordings_.find(frame_->recording);
            if (!completion.value || found == recordings_.end() || !found->second.submitted ||
                !found->second.accepted || found->second.decided || found->second.publication ||
                found->second.completion != completion.value ||
                frame_->input->View().frameId != frameId)
            {
                return Fail(error, "METEOR Scene material publication needs its exact successful native submission.");
            }
        }
        bool pending = false;
        if (ticket.IsValid())
        {
            const auto* batch = ticket.GetRecordedBatch();
            if (!batch || batch->GetFrameId() != frameId || batch->GetRecordingId() != frame_->recording ||
                batch->GetCompletionPoint().value != completion.value)
            {
                return Fail(error, "METEOR Scene publication ticket must identify its exact recorded graph batch.");
            }
            pending = !ticket.IsComplete();
        }
        RHISubmissionTicket previousTicket;
        {
            std::lock_guard lock(recordingMutex_);
            auto& recording = recordings_.at(frame_->recording);
            previousTicket = std::move(recording.ticket);
            recording.ticket = ticket;
            if (pending)
            {
                recording.publication = frame_;
            }
        }
        // 큐 접수와 material cache 게시는 별개다. 게시 검사나 tail EndFrame이
        // 실패해도 모든 자원은 이미 접수된 전체 그래프의 완료점을 보유한다.
        for (const auto& draw : frame_->draws)
        {
            draw.geometry->MarkSubmitted(completion);
            if (draw.previousGeometry && draw.previousGeometry != draw.geometry)
                draw.previousGeometry->MarkSubmitted(completion);
        }
        for (const auto& draw : frame_->shadowDraws)
        {
            draw.geometry->MarkSubmitted(completion);
        }
        if (frame_->runtimeEffects)
        {
            frame_->runtimeEffects->MarkSubmitted(completion);
        }
        std::string firstError;
        bool tracked = lookup_.TrackAcceptedSubmission(*frame_->lookup, completion, firstError);
        if (frame_->alphaLookup)
        {
            std::string alphaError;
            if (!lookup_.TrackAcceptedSubmission(*frame_->alphaLookup, completion, alphaError))
            {
                if (firstError.empty())
                {
                    firstError = std::move(alphaError);
                }
                tracked = false;
            }
        }
        if (!tracked)
        {
            return Fail(error, std::move(firstError));
        }
        if (pending)
        {
            error.clear();
            return true;
        }
        if (ticket.IsValid() && !GetRHISubmissionThread().Wait(ticket, error))
        {
            return false;
        }
        // A newer ticket can finish between PollPrograms and this call. Its
        // immediate publication must not overtake older deferred publications:
        // those frames would be rejected as stale and lose their recyclable owner.
        // The FIFO has completed older submissions when this ticket is complete.
        PollSubmittedFrames();
        return CommitSubmittedFrame(frame_, completion, error);
    }

    bool SceneHost::CommitSubmittedFrame(const own::shared_owner<const Frame>& frame, RHICompletionPoint completion,
                                         std::string& error)
    {
        if (frame->runtimeEffects)
        {
            frame->runtimeEffects->ConfirmSubmitted(completion);
        }
        const auto forwardCount = std::ranges::count_if(frame->input->Draws(), [](const auto& draw) {
            const auto& p = draw.material->generation->cooked.product.program;
            return p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u) != 0);
        });
        if (frame->alphaLookup && frame->alphaDeclared.size() != static_cast<std::size_t>(forwardCount))
        {
            return Fail(error, "METEOR alpha cache publication requires the complete blended stream.");
        }
        if (frame->alphaLookup &&
            !lookup_.PublishSubmitted(*frame->alphaLookup, frame->input->View().frameId, completion, error))
        {
            return false;
        }
        if (!lookup_.PublishSubmitted(*frame->lookup, frame->input->View().frameId, completion, error))
        {
            return false;
        }
        const auto& view = frame->input->View();
        if (frame->nextCameraHistory && frame->nextCameraHistory->HistoryOwner()->recorded.load())
        {
            auto history = frame->nextCameraHistory->HistoryOwner();
            history->completion = completion.value;
            auto& histories = cameraHistories_[{view.sceneEpoch, view.viewId}];
            if (std::ranges::none_of(histories, [&](const auto& item) { return item.pyramid == history; }))
            {
                auto historyCamera = view.camera;
                if (view.temporalFrame.camera.valid)
                    std::memcpy(&historyCamera.projection, view.temporalFrame.camera.projectionMatrix.data(), sizeof(historyCamera.projection));
                histories.push_back({std::move(history), historyCamera, view.temporalFrame.historyRevision});
            }
        }
        for (const auto& draw : frame->input->Draws())
        {
            if (!draw.selectionRevision)
            {
                continue;
            }
            const auto found = slots_.find({view.sceneEpoch, view.viewId, draw.materialSlot});
            if (found == slots_.end() || found->second->revision != draw.selectionRevision)
            {
                ++stats_.stalePublications;
                continue;
            }
            found->second->active = frame->input->MaterialOwner(draw);
            found->second->activeCoverage = draw.coverage;
            ++stats_.publications;
        }
        own::shared_owner<const Frame> publication;
        RHISubmissionTicket ticket;
        decltype(recordings_)::node_type retired;
        {
            std::lock_guard lock(recordingMutex_);
            auto& recording = recordings_.at(frame->recording);
            recording.decided = true;
            publication = std::move(recording.publication);
            ticket = std::move(recording.ticket);
            if (recording.completion != 0 && recording.completion <= completed_)
            {
                retired = recordings_.extract(frame->recording);
            }
        }
        error.clear();
        return true;
    }

    void SceneHost::PollSubmittedFrames()
    {
        struct Pending
        {
            own::shared_owner<const Frame> frame;
            RHISubmissionTicket ticket;
            RHICompletionPoint completion;
        };
        std::vector<Pending> ready;
        {
            std::lock_guard lock(recordingMutex_);
            for (const auto& [id, recording] : recordings_)
            {
                if (recording.publication && recording.ticket.IsComplete())
                {
                    ready.push_back({recording.publication, recording.ticket, {recording.completion}});
                }
            }
        }
        for (const auto& item : ready)
        {
            std::string error;
            if (GetRHISubmissionThread().Wait(item.ticket, error) &&
                CommitSubmittedFrame(item.frame, item.completion, error))
            {
                continue;
            }
            stats_.lastError = std::move(error);
            ++stats_.failedSubmissions;
            own::shared_owner<const Frame> publication;
            RHISubmissionTicket ticket;
            decltype(recordings_)::node_type retired;
            {
                std::lock_guard lock(recordingMutex_);
                const auto found = recordings_.find(item.frame->recording);
                if (found != recordings_.end())
                {
                    found->second.decided = true;
                    publication = std::move(found->second.publication);
                    ticket = std::move(found->second.ticket);
                    if (found->second.completion != 0 && found->second.completion <= completed_)
                    {
                        retired = recordings_.extract(found);
                    }
                }
            }
        }
    }

    RHIBufferHandle SceneHost::LookupStatistics() const
    {
        return HasDraws() ? frame_->lookup->Statistics() : RHIBufferHandle{};
    }

    RGHandle SceneHost::GraphLookupStatistics(const EnhancedRenderGraph& graph) const
    {
        return HasDraws() ? frame_->lookup->GraphStatistics(graph) : RGHandle{};
    }

    std::shared_ptr<const SceneLookupFrame> SceneHost::LookupFrame() const
    {
        return HasDraws() ? frame_->lookup : nullptr;
    }

    std::shared_ptr<const SceneLookupFrame> SceneHost::ForwardLookupFrame() const
    {
        return HasDraws() ? frame_->alphaLookup : nullptr;
    }

    SceneSurfaceTargets::Outputs SceneHost::ForwardSurfaceOutputs() const
    {
        return HasDraws() ? frame_->forwardGbuffer : SceneSurfaceTargets::Outputs{};
    }

    std::shared_ptr<const SceneSubsurfaceFrame> SceneHost::SubsurfaceFrame() const
    {
        return HasDraws() ? frame_->subsurface : nullptr;
    }

    std::shared_ptr<const SceneRefractionFrame> SceneHost::RefractionFrame() const
    {
        return HasDraws() ? frame_->refraction : nullptr;
    }

    std::shared_ptr<const SceneVolumeFrame> SceneHost::VolumeFrame() const
    {
        return HasDraws() ? frame_->volume : nullptr;
    }
    bool SceneHost::HasDraws() const
    {
        return frame_ && !frame_->draws.empty();
    }

    void SceneHost::ShutdownAfterIdle()
    {
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
        frame_.reset();
        cameraHistories_.clear();
        cameraOcclusion_.ShutdownAfterIdle();
        visibility_.ShutdownAfterIdle();
        lookup_.ShutdownAfterIdle();
        runtimeEffects_.ShutdownAfterIdle();
        subsurface_.ShutdownAfterIdle();
        refraction_.ShutdownAfterIdle();
        volume_.ShutdownAfterIdle();
        programs_.clear();
        bindings_.Clear();
        indirectCommands_ = {};
        hybridPipelines_ = {};
        materialWorkPipelines_ = {};
        geometry_ = {};
        device_ = nullptr;
        programDevice_ = nullptr;
        preparations_.clear();
        slots_.clear();
        decltype(recordings_) retired;
        {
            std::lock_guard lock(recordingMutex_);
            retired.swap(recordings_);
            completed_ = 0;
        }
        stats_ = {};
        selectionSerial_ = 0;
        selectionDeferred_ = false;
    }

    void SceneHost::OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion)
    {
        std::lock_guard lock(recordingMutex_);
        const auto found = recordings_.find(recording);
        if (found != recordings_.end())
        {
            found->second.submitted = true;
            if (!found->second.accepted)
            {
                found->second.completion = completion.value;
            }
        }
    }

    void SceneHost::OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion)
    {
        std::lock_guard lock(recordingMutex_);
        const auto found = recordings_.find(recording);
        if (found == recordings_.end())
        {
            return;
        }
        auto& owner = found->second;
        // Queue admission owns the frame even when later publication or tail
        // work fails. An accepted unknown completion stays quarantined to idle.
        owner.completion = owner.accepted
            ? (owner.completion == 0 || completion.value == 0 ? 0 : (std::max)(owner.completion, completion.value))
            : completion.value;
        owner.submitted = true;
        owner.accepted = true;
    }

    void SceneHost::OnUploadCompleted(std::uint64_t completed)
    {
        geometry_.NotifyCompleted(completed);
        std::vector<std::vector<own::shared_owner<const Frame>>> released;
        decltype(recordings_) retired;
        {
            std::lock_guard lock(recordingMutex_);
            completed_ = (std::max)(completed_, completed);
            released.reserve(recordings_.size());
            for (auto it = recordings_.begin(); it != recordings_.end();)
            {
                auto& owner = it->second;
                if (owner.accepted && owner.completion != 0 && owner.completion <= completed_)
                {
                    released.push_back(std::move(owner.owners));
                    if (owner.decided)
                    {
                        retired.insert(recordings_.extract(it++));
                        continue;
                    }
                }
                ++it;
            }
        }
    }

    void SceneHost::OnUploadAborted(std::uint64_t recording)
    {
        decltype(recordings_)::node_type retired;
        {
            std::lock_guard lock(recordingMutex_);
            const auto found = recordings_.find(recording);
            if (found != recordings_.end() && !found->second.accepted)
            {
                retired = recordings_.extract(found);
            }
        }
    }
    void SceneHost::OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion)
    {
        decltype(recordings_)::node_type retired;
        {
            std::lock_guard lock(recordingMutex_);
            const auto found = recordings_.find(recording);
            if (found != recordings_.end() && !found->second.accepted &&
                (!found->second.submitted || found->second.completion == completion.value))
            {
                retired = recordings_.extract(found);
            }
        }
    }
} // namespace material_graph
