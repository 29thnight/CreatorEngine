#include "MaterialGraphSceneHost.h"
#include "Render/Graph/ShadowMath.h"
#include "MaterialGraphSceneAccess.h"
#include "MaterialGraphSceneCompiler.h"

#include "PathFinder.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "RHI/RHIShaderSource.h"

#include <algorithm>
#include <set>
#include <atomic>
#include <bit>
#include <cstring>
#include <cstdio>
#include <stdexcept>

namespace material_graph
{
    namespace
    {
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
            "LX Scene program preparation exceeds its 64-request admission budget.";
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
        };
        static_assert(sizeof(SceneConstants) == 4608);
        static_assert(offsetof(SceneConstants, lights) == 368);

        struct SceneShadowConstants
        {
            math::matrix4x4 viewProjection;
            std::uint32_t coverage{};
            float cutoff{};
            std::uint32_t padding[2]{};
        };
        static_assert(sizeof(SceneShadowConstants) == 80);

        std::array<RGHandle, 5> Colors(const EnhancedGBufferPass::Outputs& outputs)
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
        std::array<LX::Runtime::GraphicsPipeline, 2> gbuffer, depth, color, blendedColor, temporal;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> lookup, blendedLookup;
        std::array<LX::Runtime::GraphicsPipeline, 2> subsurface;
        std::array<LX::Runtime::GraphicsPipeline, 2> refraction;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> runtimeEffects;
        PassLayout volumeLayout;
        LX::Runtime::ComputePipeline volume;
        RHIShaderBlob meshShader, shadowMeshShader;
        std::map<uint64_t, RHIPipelineHandle> meshPipelines;
        uint32_t meshletRoot{}, temporalPreviousRoot{}, shadowMeshletRoot{2};
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
            std::uint32_t shadowVisibilityBin{UINT32_MAX};
            bool cameraVisible{};
            std::array<RHIBufferSlice, 3> shadowConstants;
            std::array<bool, 3> shadowVisible{};
            bool doubleSided{}, blended{};
            std::size_t inputIndex{};
        };
        IRenderDeviceServices* device{};
        bool allowMeshlets{ true };
        std::uint64_t recording{}, descriptors{};
        own::shared_owner<const SceneViewInput> input;
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
        std::shared_ptr<const GpuGeometryVisibility::Frame> visibility;
        std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> shadowVisibility;
        RHITextureHandle environment;
        bool shadow{};
        mutable std::atomic<uint32_t> shadowDrawCount{};
        // One writer: LX.Scene.GBuffer recording. Consumers read after the
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
        mutable EnhancedGBufferPass::Outputs gbuffer;
        mutable EnhancedGBufferPass::Outputs forwardGbuffer;
        mutable bool colorDeclared{};
        mutable bool volumeDeclared{};
        mutable bool gbufferDeclared{}, shadowDeclared{};
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
                                      const EnhancedGBufferPass::Outputs& inputs, RGHandle lighting,
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
                throw std::runtime_error("LX Scene Decal binding failed.");
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
            return allowMeshlets && draw.program->meshEnabled && !draw.blended && draw.geometry->MeshletCount() > 0 &&
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
                throw std::runtime_error("LX Scene meshlet pipeline was not prepared.");
            }
            return found->second;
        }

        bool UsesVisibility(const Draw& draw) const
        {
            if (visibility && draw.visibilityBin != UINT32_MAX)
            {
                return true;
            }
            if (device->GetIndirectDrawCapabilities().indexedDraw)
            {
                throw std::runtime_error("LX Scene raster draw is missing its prepared GPU visibility bin.");
            }
            return false;
        }

        void AddVisibilityReads(EnhancedRenderGraph& graph,
                                std::vector<EnhancedRenderGraph::RGPassUsage>& uses) const
        {
            if (visibility)
            {
                visibility->AddReadUsages(graph, uses);
            }
        }

        void BindGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            // Runtime IBL owns the preceding two slots; material bindings follow
            // this final host slot and are resolved by the reflected PassLayout.
            const unsigned index = 15 + (draw.program->hasSpecial ? 2 : 0) +
                                   (draw.program->hasTransmission ? 1 : 0);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, index,
                UsesVisibility(draw) ? visibility->VisibleIds(draw.visibleIdOffset, 1) : draw.visibleOwner);
        }

        void DrawGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            if (UsesMeshlets(draw))
            {
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->meshletRoot, draw.geometry->Meshlets());
                const bool submitted = UsesVisibility(draw)
                    ? encoder.DispatchMeshIndirect(visibility->Arguments(), visibility->ArgsOffset(draw.visibilityBin))
                    : encoder.DispatchMesh(draw.geometry->MeshletCount(), 1, 1);
                if (!submitted)
                {
                    throw std::runtime_error("LX Scene meshlet dispatch failed.");
                }
                static std::atomic<bool> reported{};
                if (!reported.exchange(true))
                {
                    std::printf("[lx.meshlets] surface dispatch groups=%u indirect=%u\n", draw.geometry->MeshletCount(), unsigned(UsesVisibility(draw)));
                }
                return;
            }
            encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
            if (UsesVisibility(draw))
            {
                // Each bin has exactly one owner. GPU rejection changes only its
                // instanceCount (0/1), never the CPU's globally merged draw order.
                if (!encoder.DrawIndexedIndirect(visibility->Arguments(), visibility->ArgsOffset(draw.visibilityBin)))
                {
                    throw std::runtime_error("LX Scene indexed indirect submission failed.");
                }
            }
            else
            {
                // Compatibility devices without indexed indirect retain the same
                // material/effect pipelines and per-draw identity owner binding.
                encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
            }
        }

        void CheckCurrent(const EnhancedRenderGraph* currentGraph = nullptr) const
        {
            if (device->GetCurrentUploadRecordingId() != recording ||
                device->GetDescriptorVersionToken() != descriptors)
            {
                throw std::runtime_error("LX Scene frame has stale upload/descriptor ownership.");
            }
            if (currentGraph && (graph != currentGraph || currentGraph->ResourceEpoch() != graphEpoch))
            {
                throw std::runtime_error("LX Scene frame belongs to a reset graph.");
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
            return Fail(error, "LX Scene program preparation needs its owning device and immutable generation.");
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
            return reject(diagnostics.empty() ? "LX Scene route rejected." : diagnostics.front().message);
        }
        if (product.program.volume &&
            product.program.slang.find("#define LX_MATERIAL_VOLUME_HOMOGENEOUS 1\n") == std::string::npos)
        {
            return reject(
                "LX Scene Volume requires homogeneous coefficients; spatially varying Volume is unsupported.");
        }
        static std::atomic<std::uint64_t> serial{};
        auto work = own::make_shared<Preparation::Work>();
        work->generation = preparation->generation;
        work->backend = backend;
        if (product.program.semanticKey.ends_with(SceneHostIdentity))
        {
            if (!LoadSceneShaders(product, backend, *work, error))
            {
                return reject(error);
            }
            work->cooked = true;
        }
        else
        {
            if (!InternalPath::GetInstance()->AssetAuthoringEnabled)
            {
                return reject("LX Player requires the complete cooked Scene host stage set.");
            }
            auto directory = std::filesystem::path(InternalPath::GetInstance()->CacheRoot);
            if (directory.empty())
            {
                directory = std::filesystem::path(InternalPath::GetInstance()->BaseProjectPath) / "Cache";
            }
            work->file = directory / "LXScene" / ("scene-" + std::to_string(++serial) + ".slang");
            work->shaderDirectory = RHIShaderSource::Resolve("");
        }
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
                    char meshFlag[8]{};
                    size_t meshFlagBytes{};
                    getenv_s(&meshFlagBytes, meshFlag, sizeof(meshFlag), "CREATOR_LX_MESHLETS");
                    const auto caps = context.resources->GetMeshShaderCapabilities();
                    candidate->meshEnabled = std::strcmp(meshFlag, "0") != 0 && caps.meshShader && caps.meshIndirect &&
                        caps.maxOutputVertices >= 64 && caps.maxOutputPrimitives >= 126 &&
                        caps.maxThreadsPerGroup >= 64 && caps.maxThreadGroupSizeX >= 64 &&
                        caps.maxOutputMemoryBytes >= 8192;
                    candidate->meshShader = compiled->mesh.bytecode;
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
                                                                  RHILayout::Srv(0, RHIShaderVisibility::All), RHILayout::Srv(155)};
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
                            desc.rtvFormats[i] = EnhancedGBufferPass::GetRenderTargetFormat(i);
                        }
                        if (!prepare(candidate->gbuffer[side], desc, "LXSceneVS", "LXSceneGBufferPS"))
                        {
                            return false;
                        }
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
                    --pollsRemaining;
                    if (request->Poll(*context.psoManager, error) == RHIPipelineRequestState::Failed)
                    {
                        reject(error.empty() ? "LX Scene native PSO preparation failed." : error);
                    }
                    break;
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
                        const auto& shader = request == &program.shadow ? program.shadowMeshShader : program.meshShader;
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
                            reject("LX Scene meshlet PSO creation failed: " + error);
                            break;
                        }
                        program.meshPipelines.emplace(indexed.id, pipeline);
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
                    std::printf("[lx.scene.program] source=cooked graph=%s ready=%zu sceneCompiles=%llu\n",
                                Uuid::ToString(preparation->generation->assetId.value).c_str(), programs_.size(),
                                static_cast<unsigned long long>(stats_.compileSubmissions));
                    std::fflush(stdout);
                }
                preparation->program.reset();
            }
        }
        std::erase_if(preparations_, [&](const auto& item) { return IsProgramReady(item->generation, item->backend); });
        unsigned workersRemaining = 4;
        for (const auto& preparation : preparations_)
        {
            if (preparation->job.valid() && !preparation->job.is_complete())
            {
                --workersRemaining;
            }
        }
        for (const auto& preparation : preparations_)
        {
            if (workersRemaining && preparation->error.empty() && !preparation->checked && !preparation->job.valid())
            {
                const auto work = preparation->work;
                try
                {
                    preparation->job = scheduler_.submit([work]() -> bool {
                        work->worker = thread_pool::is_worker_thread();
                        VerifiedProduct verified;
                        const auto& product = work->generation->cooked.product;
                        if (!CompileSceneProduct(product.program, work->shaderDirectory, work->file, {}, verified,
                                                 work->error, {}, work->backend) ||
                            verified.layout != product.layout)
                        {
                            return Fail(work->error,
                                        work->error.empty()
                                            ? "LX Scene shader reflection differs from the exact instance layout."
                                            : work->error);
                        }
                        if (!LoadSceneShaders(verified, work->backend, *work, work->error))
                        {
                            return false;
                        }
                        work->verified = own::make_unique<const VerifiedProduct>(std::move(verified));
                        return true;
                    });
                    ++stats_.compileSubmissions;
                }
                catch (const std::exception& exception)
                {
                    preparation->error = exception.what();
                    stats_.lastError = preparation->error;
                    ++stats_.failedPreparations;
                }
                --workersRemaining;
            }
        }
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
        return Fail(error, "LX Scene program is not ready; request and poll before recording the frame.");
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
            return Fail(error, "LX Scene selection needs the current identified view.");
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
                return Fail(error, "LX Scene Material slots must identify one exact instance and coverage per view.");
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
                return Fail(error, "LX Scene material has no Surface or Volume output.");
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
                return Fail(error, "LX Scene requested material program is still preparing.");
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
            return Fail(error, "LX Scene texture residency needs an active owner upload recording.");
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
                    return Fail(error, error.empty() ? "LX Scene texture residency upload failed." : error);
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
                            RHITextureHandle source, bool allowMeshlets)
    {
        ce::profile_scope profile{ce::marker<"MaterialGraphScenePrepare">()};
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
            return Fail(error, "LX Scene host needs the exact current view/frame/device services.");
        }
        if (std::uint64_t(context.width) * context.height > budget.pixels || budget.pixels > 4096u * 4096u ||
            input->Draws().size() > budget.draws || budget.draws > 4096)
        {
            return Fail(error, "LX Scene composition exceeds its configured viewport/draw budget.");
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
        candidate->allowMeshlets = allowMeshlets;
        candidate->recording = device_->GetCurrentUploadRecordingId();
        candidate->descriptors = device_->GetDescriptorVersionToken();
        candidate->input = std::move(input);
        candidate->environment = environment;
        candidate->shadow = shadow.enabled;
        const std::array<std::byte, SceneVolumeConstantsBytes> emptyMedium{};
        candidate->emptyVolumeConstants = device_->UploadConstants(emptyMedium.data(), emptyMedium.size());
        candidate->emptyVolumeBuffer = device_->AllocateUpload({48, RHIUploadUsage::Raw, 16});
        if (!candidate->emptyVolumeConstants.IsValid() || !candidate->emptyVolumeBuffer.IsWritable())
        {
            return Fail(error, "LX Scene empty medium allocation failed.");
        }
        std::memset(candidate->emptyVolumeBuffer.cpuAddress, 0, 48);
        const RHIBindingDesc emptyVolumeBindings[]{RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull(),
                                                   RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull()};
        candidate->emptyVolumeBindings = device_->CreateBindings(emptyVolumeBindings);
        if (!candidate->emptyVolumeBindings.IsValid())
        {
            return Fail(error, "LX Scene empty medium bindings could not be allocated.");
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
                return Fail(error, "LX Scene Decal constants allocation failed.");
            }
        }
        std::uint32_t owner = 0x80000000u;
        std::vector<GpuGeometryVisibility::Candidate> visibilityCandidates;
        std::vector<GpuGeometryVisibility::Bin> visibilityBins;
        std::vector<GpuGeometryVisibility::Candidate> shadowCandidates;
        std::vector<GpuGeometryVisibility::Bin> shadowBins;
        const bool indexedIndirect = device_->GetIndirectDrawCapabilities().indexedDraw;
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
                    return Fail(error, "LX Scene Volume coefficient constants allocation failed.");
                }
                candidate->volumeBindings.push_back(std::move(volume));
            }
            SceneConstants constants{};
            constants.viewProjection = math::transpose(candidate->input->ViewProjection());
            const auto& eye = candidate->input->Surface().eye;
            constants.eye = math::vector4(eye[0], eye[1], eye[2], 1);
            constants.owner = ++owner;
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
            const auto uploaded = device_->UploadConstants(&constants, sizeof(constants));
            if (!uploaded.IsValid())
            {
                return Fail(error, "LX Scene constants allocation failed.");
            }
            const auto visibleOwner = device_->AllocateUpload({sizeof(constants.owner), RHIUploadUsage::BufferCopy, 256});
            if (!visibleOwner.IsValid() || !visibleOwner.IsWritable())
            {
                return Fail(error, "LX Scene visible owner allocation failed.");
            }
            std::memcpy(visibleOwner.cpuAddress, &constants.owner, sizeof(constants.owner));
            const auto reference = [&] {
                if (!ordered)
                {
                    return RHIBufferSlice{};
                }
                constants.padding[0] = 2;
                return device_->UploadConstants(&constants, sizeof(constants));
            }();
            if (ordered && !reference.IsValid())
            {
                return Fail(error, "LX reference constants allocation failed.");
            }
            bool shadowEnabled{};
            std::array<RHIBufferSlice, 3> shadowConstants;
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
                    shadowConstants[cascade] = device_->UploadConstants(&shadowValues, sizeof(shadowValues));
                    if (!shadowConstants[cascade].IsValid())
                    {
                        return Fail(error, "LX Scene shadow constants allocation failed.");
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
                for (unsigned cascade = 0; cascade < 3; ++cascade)
                {
                    item.shadowVisible[cascade] = !indexedIndirect &&
                        shadow_math::IntersectsClip({draw.shadowCenter, draw.shadowRadius},
                                                   shadow.lightViewProjection[cascade]);
                }
                item.constants = uploaded;
                item.visibleOwner = visibleOwner;
                item.cameraVisible = !indexedIndirect &&
                    shadow_math::IntersectsClip({draw.shadowCenter, draw.shadowRadius},
                                               candidate->input->ViewProjection());
                item.referenceConstants = reference;
                item.doubleSided = (draw.coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0;
                if (!geometry_.Prepare(*device_, chunk.input, item.geometry, error, true))
                {
                    return false;
                }
                const auto chunkIndex = static_cast<std::size_t>(&chunk - draw.geometry->Chunks().data());
                if (draw.previousGeometry && draw.previousGeometry != draw.geometry && chunkIndex < draw.previousGeometry->Chunks().size())
                {
                    if (!geometry_.Prepare(*device_, draw.previousGeometry->Chunks()[chunkIndex].input,
                        item.previousGeometry, error, true)) return false;
                }
                else item.previousGeometry = item.geometry;
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
                if (indexedIndirect && rasterGeometry)
                {
                    item.visibilityBin = static_cast<std::uint32_t>(visibilityBins.size());
                    item.visibleIdOffset = item.visibilityBin * GpuGeometryVisibility::kOutputAlignment;
                    visibilityBins.push_back(candidate->UsesMeshlets(item)
                        ? GpuGeometryVisibility::MeshDispatchBin(item.geometry->MeshletCount())
                        : GpuGeometryVisibility::Bin{source.indexCount, 0, 0, 0});
                    visibilityCandidates.push_back({
                        math::vector4{draw.shadowCenter.x, draw.shadowCenter.y, draw.shadowCenter.z, draw.shadowRadius},
                        item.visibilityBin, constants.owner, item.visibleIdOffset,
                        ordered ? GpuGeometryVisibility::kNoOcclusion : 0u});
                }
                if (indexedIndirect && shadowEnabled && !draw.geometryLod)
                {
                    // Independent caster candidates are not camera/HZB filtered.
                    // Each cascade keeps the existing light clip-volume contract.
                    item.shadowVisibilityBin = static_cast<std::uint32_t>(shadowBins.size());
                    shadowBins.push_back(candidate->UsesMeshlets(item)
                        ? GpuGeometryVisibility::MeshDispatchBin(item.geometry->MeshletCount())
                        : GpuGeometryVisibility::Bin{source.indexCount, 0, 0, 0});
                    shadowCandidates.push_back({
                        math::vector4{draw.shadowCenter.x, draw.shadowCenter.y, draw.shadowCenter.z, draw.shadowRadius},
                        item.shadowVisibilityBin, constants.owner,
                        item.shadowVisibilityBin * GpuGeometryVisibility::kOutputAlignment, 0});
                }
                const auto bytes = std::size_t(source.indexCount) * sizeof(std::uint32_t);
                item.indices = item.geometry->Indices();
                if (!item.indices.IsValid())
                {
                    item.indices = device_->AllocateUpload({bytes, RHIUploadUsage::IndexData, alignof(std::uint32_t)});
                    if (!item.indices.IsValid() || !item.indices.cpuAddress)
                    {
                        return Fail(error, "LX Scene index upload failed.");
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
                    for (unsigned cascade = 0; cascade < 3; ++cascade)
                    {
                        item.shadowVisible[cascade] = !indexedIndirect &&
                            shadow_math::IntersectsClip({draw.shadowCenter, draw.shadowRadius}, shadow.lightViewProjection[cascade]);
                    }
                    if (!geometry_.Prepare(*device_, chunk.input, item.geometry, error, true))
                    {
                        return false;
                    }
                    // Shadow-only LOD0 geometry is never consumed by the motion
                    // pass and does not share the visible LOD's chunk indexing.
                    const auto& source = chunk.input->Geometry();
                    if (indexedIndirect)
                    {
                        item.shadowVisibilityBin = static_cast<uint32_t>(shadowBins.size());
                        shadowBins.push_back(candidate->UsesMeshlets(item)
                            ? GpuGeometryVisibility::MeshDispatchBin(item.geometry->MeshletCount())
                            : GpuGeometryVisibility::Bin{source.indexCount, 0, 0, 0});
                        shadowCandidates.push_back({
                            math::vector4{draw.shadowCenter.x, draw.shadowCenter.y, draw.shadowCenter.z, draw.shadowRadius},
                            item.shadowVisibilityBin, constants.owner,
                            item.shadowVisibilityBin * GpuGeometryVisibility::kOutputAlignment, 0});
                    }
                    item.indices = item.geometry->Indices();
                    if (!item.indices.IsValid())
                    {
                        const auto bytes = size_t(source.indexCount) * sizeof(uint32_t);
                        item.indices = device_->AllocateUpload({bytes, RHIUploadUsage::IndexData, alignof(uint32_t)});
                        if (!item.indices.IsWritable())
                        {
                            return Fail(error, "LX Scene shadow LOD0 index upload failed.");
                        }
                        std::memcpy(item.indices.cpuAddress, source.indexData, bytes);
                    }
                    candidate->shadowDraws.push_back(std::move(item));
                }
            }
        }
        if (!visibility_.Prepare(context, candidate->input->ViewProjection(), visibilityCandidates, visibilityBins,
                                 candidate->visibility, error, true))
        {
            return false;
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
        for (unsigned cascade = 0; cascade < candidate->shadowVisibility.size(); ++cascade)
        {
            if (!visibility_.Prepare(context, shadow.lightViewProjection[cascade], shadowCandidates, shadowBins,
                                     candidate->shadowVisibility[cascade], error))
            {
                return false;
            }
        }
        try
        {
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
        std::set<const MeshSurfaceBatch*> declaredGeometry;
        for (const auto& draw : frame->draws)
        {
            declaredGeometry.insert(draw.geometry.get());
            std::string error;
            if (!draw.geometry->Declare(graph, error))
            {
                throw std::runtime_error(error);
            }
            if (draw.previousGeometry && declaredGeometry.insert(draw.previousGeometry.get()).second &&
                !draw.previousGeometry->Declare(graph, error)) throw std::runtime_error(error);
            for (const auto& texture : draw.bindings->resources.textures)
            {
                auto handle = graph.FindImportedTexture(texture.resource);
                if (!handle.IsValid())
                {
                    handle =
                        graph.ImportTexture(texture.resource, RHIResourceState::PixelShaderResource, "LX.Scene.Image");
                }
            }
        }
        for (const auto& draw : frame->shadowDraws)
        {
            if (!declaredGeometry.insert(draw.geometry.get()).second)
            {
                continue;
            }
            std::string error;
            if (!draw.geometry->Declare(graph, error))
            {
                throw std::runtime_error(error);
            }
        }
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

    uint32_t SceneHost::ShadowDrawCount() const
    {
        return frame_ ? frame_->shadowDrawCount.load(std::memory_order_relaxed) : 0;
    }

    GpuGeometryVisibility::PreparedStats SceneHost::CameraVisibilityStats() const
    {
        return frame_ && frame_->visibility ? frame_->visibility->GetPreparedStats()
                                            : GpuGeometryVisibility::PreparedStats{};
    }

    std::array<GpuGeometryVisibility::PreparedStats, 3> SceneHost::ShadowVisibilityStats() const
    {
        std::array<GpuGeometryVisibility::PreparedStats, 3> result{};
        if (frame_)
        {
            for (unsigned cascade = 0; cascade < result.size(); ++cascade)
            {
                if (frame_->shadowVisibility[cascade])
                {
                    result[cascade] = frame_->shadowVisibility[cascade]->GetPreparedStats();
                }
            }
        }
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
                const bool indirect = frame_->shadowVisibility[cascade] && draw.shadowVisibilityBin != UINT32_MAX;
                counts[cascade] += (indirect || draw.shadowVisible[cascade]) ? 1u : 0u;
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
            throw std::runtime_error("LX Scene shadow requires one declaration per prepared frame.");
        }
        DeclareGeometry(graph);
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            shadowMap = graph.Modify(shadowMap);
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{{shadowMap, RHIResourceState::DepthWrite, modifyAccess}};
        for (const auto& visibility : frame->shadowVisibility)
        {
            if (visibility)
            {
                visibility->Declare(graph);
                visibility->AddReadUsages(graph, uses);
            }
        }
        for (const auto& draw : frame->shadowDraws)
        {
            if (!draw.shadowEnabled)
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
        NormalizeSceneReads(graph, uses);
        graph.AddPass("LX.Scene.Shadow", uses, [frame, shadowMap](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            auto& encoder = *execution.encoder;
            const auto map = execution.ResolveHandle(shadowMap);
            const auto description = frame->device->DescribeTexture(map);
            if (description.depthOrArraySize != 3 || description.format != RHIFormat::D32Float)
            {
                throw std::runtime_error("LX Scene shadow needs its three D32 cascades.");
            }
            encoder.SetViewportAndScissor(description.width, description.height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                const auto depth = RHIDepthTargetDesc::DepthSlice(map, RHIFormat::D32Float, cascade);
                const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depth);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("LX Scene cascade target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                RHIPipelineHandle boundPipeline{};
                // Address identity is local to this pass. The captured frame pins
                // every compared binding for the entire callback.
                const RenderBindings* boundBindings = nullptr;
                for (const auto& draw : frame->shadowDraws)
                {
                    const auto& visibility = frame->shadowVisibility[cascade];
                    const bool indirect = visibility && draw.shadowVisibilityBin != UINT32_MAX;
                    if (!draw.shadowEnabled || (!indirect && !draw.shadowVisible[cascade]))
                    {
                        continue;
                    }
                    const auto pipeline = frame->RasterPipeline(draw, draw.program->shadow.GetHandle());
                    if (pipeline != boundPipeline)
                    {
                        encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);
                        boundPipeline = pipeline;
                        boundBindings = nullptr;
                    }
                    std::string error;
                    if (boundBindings != std::addressof(*draw.bindings) &&
                        !RenderBindingCache::BindPass(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  draw.program->shadowLayout,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    boundBindings = std::addressof(*draw.bindings);
                    encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.shadowConstants[cascade]);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                    encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                    if (frame->UsesMeshlets(draw))
                    {
                        encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->shadowMeshletRoot, draw.geometry->Meshlets());
                        const bool submitted = indirect
                            ? encoder.DispatchMeshIndirect(visibility->Arguments(), visibility->ArgsOffset(draw.shadowVisibilityBin))
                            : encoder.DispatchMesh(draw.geometry->MeshletCount(), 1, 1);
                        if (!submitted)
                        {
                            throw std::runtime_error("LX Scene shadow meshlet dispatch failed.");
                        }
                        static std::atomic<bool> reported{};
                        if (!reported.exchange(true))
                        {
                            std::printf("[lx.meshlets] shadow dispatch groups=%u indirect=%u\n", draw.geometry->MeshletCount(), unsigned(indirect));
                        }
                    }
                    else if (indirect)
                    {
                        if (!encoder.DrawIndexedIndirect(visibility->Arguments(),
                                                         visibility->ArgsOffset(draw.shadowVisibilityBin)))
                        {
                            throw std::runtime_error("LX Scene shadow indexed indirect submission failed.");
                        }
                    }
                    else
                    {
                        encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
                    }
                    frame->shadowDrawCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
        frame->shadowDeclared = true;
        return shadowMap;
    }

    std::array<RGHandle, 5> SceneHost::DeclareTemporal(EnhancedRenderGraph& graph,
        std::array<RGHandle, 5> targets) const
    {
        const auto frame = frame_;
        if (!frame || frame->draws.empty()) return targets;
        DeclareGeometry(graph);
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
        NormalizeSceneReads(graph, uses);
        graph.AddPass("Temporal.ObjectMotion", uses, [frame, targets](const auto& execution) {
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
            for (const auto& draw : frame->draws)
            {
                if (!draw.program->hasSurface) continue;
                encoder.SetPipeline(RHIBindPoint::Graphics, draw.program->temporal[draw.doubleSided].GetHandle());
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                    throw std::runtime_error(error);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->temporalPreviousRoot,
                    RHIBufferSlice::Whole(draw.previousGeometry->Buffer()));
                const unsigned ownerRoot = 15 + (draw.program->hasSpecial ? 2 : 0) + (draw.program->hasTransmission ? 1 : 0);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, ownerRoot, draw.visibleOwner);
                encoder.SetIndexBuffer(draw.indices, RHIFormat::R32Uint);
                // Dedicated indexed replay keeps vertex correspondence independent
                // of meshlet/indirect compaction and preserves every alpha surface.
                encoder.DrawIndexed(draw.geometry->Input()->Geometry().indexCount, 1);
            }
        }, true);
        return targets;
    }

    EnhancedGBufferPass::Outputs SceneHost::DeclareGBuffer(EnhancedRenderGraph& graph,
                                                           const EnhancedGBufferPass::Outputs& incoming,
                                                           bool hasOccluderDepth) const
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
            throw std::runtime_error("LX Scene GBuffer requires one declaration per prepared frame.");
        }
        DeclareGeometry(graph);
        auto occluderDepth = incoming.depth;
        char occlusionFlag[8]{};
        size_t occlusionFlagBytes{};
        getenv_s(&occlusionFlagBytes, occlusionFlag, sizeof(occlusionFlag), "CREATOR_LX_HZB");
        const bool occlusionEnabled = std::strcmp(occlusionFlag, "0") != 0;
        const bool depthPrepass = occlusionEnabled && !hasOccluderDepth && frame->visibility &&
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
            depthDescription.name = "LX.Scene.OccluderDepth";
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
            NormalizeSceneReads(graph, depthUses);
            graph.AddPass("LX.Scene.OccluderDepth", depthUses, [frame, depthHandle = occluderDepth](const auto& execution) {
                frame->CheckCurrent(execution.graph);
                auto& encoder = *execution.encoder;
                const auto depthTarget = RHIDepthTargetDesc::Depth(execution.ResolveHandle(depthHandle), RHIFormat::D32Float);
                const auto targets = frame->device->CreateRenderTargets(std::span<const RHITextureHandle>{}, &depthTarget);
                if (!targets.IsValid())
                {
                    throw std::runtime_error("LX Scene occluder depth target binding failed.");
                }
                encoder.BindRenderTargets(targets);
                encoder.ClearDepthTarget(targets, 1.f);
                encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                for (const auto& draw : frame->draws)
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
                        if (!encoder.DispatchMesh(draw.geometry->MeshletCount(), 1, 1))
                        {
                            throw std::runtime_error("LX Scene occluder mesh dispatch failed.");
                        }
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
            if (occlusionEnabled && (hasOccluderDepth || depthPrepass) &&
                graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
            {
                // Cull LX against the native GBuffer's earlier depth version.
                // Declare before Modify: this must never consume LX's own depth.
                frame->visibility->DeclareWithOcclusion(graph, occluderDepth);
            }
            else
            {
                frame->visibility->Declare(graph);
            }
        }
        const auto inputs = AdvanceSceneSurface(graph, incoming, RGAccessMode::ReadWrite);
        std::vector<EnhancedRenderGraph::RGPassUsage> uses;
        frame->AddVisibilityReads(graph, uses);
        for (const auto& draw : frame->draws)
        {
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
        for (const auto target : Colors(inputs))
        {
            uses.push_back({target, RHIResourceState::RenderTarget, modifyAccess});
        }
        uses.push_back({inputs.depth, RHIResourceState::DepthWrite, modifyAccess});
        NormalizeSceneReads(graph, uses);
        graph.AddPass("LX.Scene.GBuffer", uses, [frame, inputs](const auto& execution) {
            frame->CheckCurrent(execution.graph);
            frame->recordedGeometryRoutes.clear();
            auto& encoder = *execution.encoder;
            std::array<RHITextureHandle, 5> colors;
            const auto handles = Colors(inputs);
            for (unsigned i = 0; i < 5; ++i)
            {
                colors[i] = execution.ResolveHandle(handles[i]);
            }
            const auto depth = RHIDepthTargetDesc::Depth(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
            const auto targets = frame->device->CreateRenderTargets(colors, &depth);
            if (!targets.IsValid())
            {
                throw std::runtime_error("LX Scene shared GBuffer target binding failed.");
            }
            encoder.BindRenderTargets(targets);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->draws)
            {
                if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                {
                    continue;
                }
                const bool indirect = frame->UsesVisibility(draw);
                if (!indirect && !draw.cameraVisible)
                {
                    continue;
                }
                const auto sourcePipeline = draw.program->gbuffer[draw.doubleSided].GetHandle();
                const auto recordedPipeline = frame->RasterPipeline(draw, sourcePipeline);
                encoder.SetPipeline(RHIBindPoint::Graphics, recordedPipeline);
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, draw.constants);
                frame->BindGeometry(encoder, draw);
                frame->DrawGeometry(encoder, draw);
                if (frame->geometryRouteAuditEnabled)
                {
                    const auto& source = frame->input->Draws()[draw.inputIndex];
                    const bool meshShader = frame->UsesMeshlets(draw);
                    frame->recordedGeometryRoutes.push_back({source.geometryKey,
                        source.temporalObjectId, source.temporalInstanceId,
                        sourcePipeline.id, recordedPipeline.id,
                        meshShader ? draw.geometry->MeshletCount() : 0u, meshShader, indirect});
                }
            }
        });
        frame->gbufferDeclared = true;
        frame->gbuffer = inputs;
        return inputs;
    }

    void SceneHost::DeclareDecalInputs(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
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
                "LX Scene Decal requires its GBuffer, a valid snapshot and one declaration before Color.");
        }
        frame->decalBaseline = baseline;
        frame->gbuffer = inputs;
        frame->decalDeclared = true;
    }

    RGHandle SceneHost::DeclareColor(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
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
            throw std::runtime_error("LX Scene color requires its own GBuffer producer and one declaration.");
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

    RGHandle SceneHost::DeclareBlended(EnhancedRenderGraph& graph, std::size_t index, RGHandle lighting,
                                       RGHandle shadowMap, const EnhancedForwardLighting& forward) const
    {

        const auto frame = frame_;
        if (!frame || !frame->colorDeclared || !frame->alphaLookup || !lighting.IsValid() ||
            index >= frame->input->Draws().size() || !forward.lights.IsValid() || !forward.counts.IsValid() ||
            !forward.indices.IsValid() || !forward.graphCounts.IsValid() || !forward.graphIndices.IsValid())
        {
            throw std::runtime_error("Graph Blend needs its exact prepared frame and common Forward+ resources.");
        }
        frame->CheckCurrent(&graph);
        if (!frame->alphaDeclared.insert(index).second)
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
                                  shadowMap, (p.features & 0x0800u) != 0, index, forward);
        frame->alphaLookup->DeclareReady(graph);
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
            throw std::runtime_error("LX Scene Volume requires completed surface shading.");
        }
        const auto result = frame->volume->DeclareComposite(graph, lighting, depth, shadowMap);
        frame->volumeDeclared = true;
        return result;
    }
    void SceneHost::DeclareForwardGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& opaque,
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
                desc.format = EnhancedGBufferPass::GetRenderTargetFormat(i);
                desc.name = "Forward+.GraphSurface";
                *targets[i] = graph.CreateTexture(desc);
            }
            desc.format = RHIFormat::D32Float;
            desc.allowRenderTarget = false;
            desc.allowDepthStencil = true;
            desc.name = "Forward+.GraphDepth";
            frame->forwardGbuffer.depth = graph.CreateTexture(desc);
        }
        const auto writeAccess = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
        if (graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned)
        {
            frame->forwardGbuffer.depth = graph.Write(frame->forwardGbuffer.depth);
        }
        const auto copiedDepth = frame->forwardGbuffer.depth;
        graph.AddPass("Forward+.GraphDepthCopy",
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
        frame->AddVisibilityReads(graph, uses);
        NormalizeSceneReads(graph, uses);
        graph.AddPass("Forward+.GraphSurface", uses, [frame, inputs, index](const auto& execution) {
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
                throw std::runtime_error("LX Scene transmission GBuffer binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->draws)
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

    void SceneHost::DeclareRefractionCapture(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
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
        frame->AddVisibilityReads(graph, uses);
        NormalizeSceneReads(graph, uses);
        graph.AddPass("LX.Scene.RefractionCapture", uses, [frame, inputs, index](const auto& execution) {
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
                throw std::runtime_error("LX Scene refraction capture binding failed.");
            }
            auto& encoder = *execution.encoder;
            encoder.BindRenderTargets(targets);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(targets, clear);
            encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& draw : frame->draws)
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

    RGHandle SceneHost::DeclareShading(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                       RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                       bool transmissionStage, std::optional<std::size_t> blended,
                                       EnhancedForwardLighting forward) const
    {
        const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
        const auto readAccess = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
        const auto modifyAccess = explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState;

        const auto frame = frame_;
        const auto lookup = blended ? frame->alphaLookup : frame->lookup;
        const auto selected = [blended](const Frame::Draw& draw) {
            return blended ? draw.blended && draw.inputIndex == *blended : !draw.blended;
        };
        const bool ordinary = !frame->runtimeEffects || std::ranges::any_of(frame->DrawsFor(blended), [&](const auto& draw) {
            return selected(draw) && draw.program->hasSurface && !draw.program->hasSpecial &&
                   draw.program->hasTransmission == transmissionStage;
        });
        if (ordinary)
        {
            lighting = AdvanceSceneColor(graph, lighting);
        }
        std::vector<EnhancedRenderGraph::RGPassUsage> uses{
            {lighting, RHIResourceState::RenderTarget, modifyAccess},
            {inputs.depth, RHIResourceState::DepthRead, readAccess},
            {inputs.bitmask, RHIResourceState::ShaderResource, readAccess},
            {ambientOcclusion, RHIResourceState::ShaderResource, readAccess}};
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
                    graph.ImportTexture(frame->environment, RHIResourceState::PixelShaderResource, "LX.Scene.HDR");
            }
            uses.push_back({environment, RHIResourceState::PixelShaderResource, readAccess});
        }
        if (frame->shadow)
        {
            uses.push_back({shadowMap, RHIResourceState::ShaderResource, readAccess});
        }
        for (const auto& draw : frame->DrawsFor(blended))
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
            frame->AddDecalUses(captureUses, transmissionStage || blended.has_value(), lookupRead);
            const unsigned first = part ? 8 : 0, count = part ? 3 : 8;
            const auto lookupInputs = lookup->DeclareCaptureOutputs(graph, first, count);
            for (unsigned i = first; i < first + count; ++i)
            {
                captureUses.push_back({lookupInputs[i], RHIResourceState::RenderTarget, lookupWrite});
            }
            for (const auto& draw : frame->DrawsFor(blended))
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
            frame->AddVisibilityReads(graph, captureUses);
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                "LX.Scene.LookupCapture", captureUses,
                [frame, inputs, first, count, part, transmissionStage, blended, lookup, selected,
                 forward](const auto& execution) {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto handles = lookup->Inputs().subspan(first, count);
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto targets = frame->device->CreateRenderTargets(handles, &depth);
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
                        throw std::runtime_error("LX Scene lookup capture binding failed.");
                    }
                    encoder.BindRenderTargets(targets);
                    const float clear[]{0, 0, 0, 0};
                    encoder.ClearRenderTargets(targets, clear);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->DrawsFor(blended))
                    {
                        if (!draw.program->hasSurface || !selected(draw))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedLookup : draw.program->lookup)[part][draw.doubleSided]
                                .GetHandle()));
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        frame->BindGeometry(encoder, draw);
                        encoder.SetBindings(RHIBindPoint::Graphics, 2, table);
                        frame->BindDecal(encoder, draw, decalTable, transmissionStage || blended.has_value());
                        frame->BindForward(encoder, draw, forward);
                        frame->DrawGeometry(encoder, draw);
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
        const bool special = std::ranges::any_of(frame->DrawsFor(blended), [&](const auto& draw) {
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
            for (const auto& draw : frame->DrawsFor(blended))
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
            frame->AddVisibilityReads(graph, captureUses);
            NormalizeSceneReads(graph, captureUses);
            graph.AddPass(
                "LX.Scene.SubsurfaceCapture", captureUses,
                [frame, inputs, shadowMap, transmissionStage, blended, lookup, selected,
                 forward](const auto& execution) {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto targets = frame->device->CreateRenderTargets(frame->subsurface->Inputs(), &depth);
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
                        throw std::runtime_error("LX Scene SSS capture target binding failed.");
                    }
                    encoder.BindRenderTargets(targets);
                    const float clear[]{0, 0, 0, 0};
                    encoder.ClearRenderTargets(targets, clear);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->DrawsFor(blended))
                    {
                        if (!selected(draw) || !draw.program->hasSubsurface ||
                            (draw.program->hasTransmission && !transmissionStage))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, draw.program->subsurface[draw.doubleSided].GetHandle()));
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants
                                                                               : draw.constants);
                        frame->BindGeometry(encoder, draw);
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
                        frame->DrawGeometry(encoder, draw);
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
            frame->AddVisibilityReads(graph, uses);
            NormalizeSceneReads(graph, uses);
            graph.AddPass(
                blended             ? "Forward+.GraphBlend"
                : transmissionStage ? "LX.Scene.TransmissionColor"
                                    : "LX.Scene.Color",
                uses,
                [frame, inputs, lighting, ambientOcclusion, shadowMap, transmissionStage, blended, lookup, selected,
                 forward](const auto& execution) mutable {
                    frame->CheckCurrent(execution.graph);
                    auto& encoder = *execution.encoder;
                    const auto color = execution.ResolveHandle(lighting);
                    const auto depth =
                        RHIDepthTargetDesc::DepthReadOnly(execution.ResolveHandle(inputs.depth), RHIFormat::D32Float);
                    const auto target = frame->device->CreateRenderTargets({&color, 1}, &depth);
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
                        throw std::runtime_error("LX Scene color/depth/AO/environment binding failed.");
                    }
                    encoder.BindRenderTargets(target);
                    encoder.SetViewportAndScissor(frame->input->View().width, frame->input->View().height);
                    encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    for (const auto& draw : frame->DrawsFor(blended))
                    {
                        if (!selected(draw) || !draw.program->hasSurface ||
                            draw.program->hasTransmission != transmissionStage ||
                            (frame->runtimeEffects && draw.program->hasSpecial))
                        {
                            continue;
                        }
                        encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle()));
                        std::string error;
                        if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                      error))
                        {
                            throw std::runtime_error(error);
                        }
                        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                                  blended && forward.reference ? draw.referenceConstants : draw.constants);
                        frame->BindGeometry(encoder, draw);
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
                        frame->DrawGeometry(encoder, draw);
                    }
                });
        }
        if (frame->runtimeEffects && special)
        {
            lighting = DeclareRuntimeEffects(graph, inputs, lighting, ambientOcclusion, shadowMap,
                                             transmissionStage, blended, forward);
        }
        return lighting;
    }

    RGHandle SceneHost::DeclareRuntimeEffects(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                              RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                              bool transmissionStage, std::optional<std::size_t> blended,
                                              EnhancedForwardLighting forward) const
    {
        const auto frame = frame_;
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
            frame->AddVisibilityReads(graph, uses);
            return uses;
        };
        const auto raster = [frame, effects, lookup, inputs, ambientOcclusion, shadowMap, transmissionStage,
                             blended, selected](const EnhancedRenderGraph::ExecuteContext& execution,
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
            for (const auto& draw : frame->DrawsFor(blended))
            {
                if (!selected(draw))
                {
                    continue;
                }
                const auto pipeline = capture ? draw.program->runtimeEffects[part][draw.doubleSided].GetHandle()
                    : (blended ? draw.program->blendedColor : draw.program->color)[draw.doubleSided].GetHandle();
                encoder.SetPipeline(RHIBindPoint::Graphics, frame->RasterPipeline(draw, pipeline));
                std::string error;
                if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings, error))
                {
                    throw std::runtime_error(error);
                }
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                          blended && forward.reference ? draw.referenceConstants : draw.constants);
                frame->BindGeometry(encoder, draw);
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
                frame->DrawGeometry(encoder, draw);
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
        phases.push_back({"LX.Scene.RuntimeTileDepth",
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
            phases.push_back({part ? "LX.Scene.RuntimeEffectCapture1" : "LX.Scene.RuntimeEffectCapture0",
                              std::move(captureUses)});
        }
        auto colorUses = commonUses();
        colorUses.push_back({inputs.depth, RHIResourceState::DepthRead, RGAccessMode::Read});
        colorUses.push_back({lighting, RHIResourceState::RenderTarget, RGAccessMode::ReadWrite});
        effects->DeclareShadingInputs(graph, colorUses);
        NormalizeSceneReads(graph, colorUses);
        phases.push_back({"LX.Scene.RuntimeEffectColor", std::move(colorUses)});
        graph.AddRepeatedPass("LX.Scene.RuntimeEffects", external, phases,
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
                return Fail(error, "LX Scene material publication needs its exact successful native submission.");
            }
        }
        bool pending = false;
        if (ticket.IsValid())
        {
            const auto* batch = ticket.GetRecordedBatch();
            if (!batch || batch->GetFrameId() != frameId || batch->GetRecordingId() != frame_->recording ||
                batch->GetCompletionPoint().value != completion.value)
            {
                return Fail(error, "LX Scene publication ticket must identify its exact recorded graph batch.");
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
            return Fail(error, "LX alpha cache publication requires the complete Forward+ stream.");
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

    EnhancedGBufferPass::Outputs SceneHost::ForwardSurfaceOutputs() const
    {
        return HasDraws() ? frame_->forwardGbuffer : EnhancedGBufferPass::Outputs{};
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
        visibility_.ShutdownAfterIdle();
        lookup_.ShutdownAfterIdle();
        runtimeEffects_.ShutdownAfterIdle();
        subsurface_.ShutdownAfterIdle();
        refraction_.ShutdownAfterIdle();
        volume_.ShutdownAfterIdle();
        programs_.clear();
        bindings_.Clear();
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
