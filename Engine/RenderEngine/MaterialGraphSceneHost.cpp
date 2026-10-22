#include "../Utility_Framework/Ownership.h"
#include "MaterialGraphSceneHost.h"
#include "MaterialGraphGeometryContract.h"
#include "MaterialGraphNativeAdmission.h"
#include "Render/Graph/ShadowMath.h"
#include "MaterialGraphSceneAccess.h"
#include "MaterialGraphSceneCompiler.h"
#include "MaterialGraphIndirectBatch.h"
#include "MaterialGraphWork.h"
#include "MaterialGraphPreparationTasks.h"
#include "RenderArena.h"

#include "PathFinder.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include "RHI/RHIShaderSource.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
    namespace
    {
        // Diagnostic only (CREATOR_METEOR_SHADOW_MESHLET_AUDIT=1). Replays the shadow
        // mesh shader's per-meshlet clip rejection (LXSceneMeshletVisible) on the
        // CPU, so the work spent on meshlets that the shader discards is countable.
        bool ShadowMeshletAuditEnabled()
        {
            static const bool enabled = []
            {
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

        // Candidate-only static geometry diagnostic over every cooked LOD.
        // It does not select GPU LOD or measure final GPU survivors.
        void AuditShadowMeshlets(const MeshSurfaceInput& input, const math::matrix4x4& lightViewProjection,
                                 ShadowMeshletAudit& audit)
        {
            const auto& geometry = input.Geometry();
            const auto& topology = input.Meshlets();
            if (!topology.HasMeshlets())
            {
                return;
            }
            const math::matrix4x4 toClip = input.World() * lightViewProjection;
            const auto& world = input.World();
            const auto vertexCount = geometry.vertexBytes / geometry.vertexStride;
            const auto position = [&](std::uint32_t index)
            {
                float local[3];
                std::memcpy(local, static_cast<const std::byte*>(geometry.vertexData) + index * geometry.vertexStride,
                            sizeof(local));
                return std::array<float, 3>{local[0], local[1], local[2]};
            };
            const auto transform = [](const std::array<float, 3>& p, const math::matrix4x4& m)
            {
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
                    const float epsilon =
                        (std::max)(1.f, (std::max)(std::abs(clip[3]),
                                                   (std::max)(std::abs(clip[0]),
                                                              (std::max)(std::abs(clip[1]), std::abs(clip[2]))))) *
                        0.00001f;
                    std::uint32_t mask = 0u;
                    if (std::isfinite(clip[0]) && std::isfinite(clip[1]) && std::isfinite(clip[2]) &&
                        std::isfinite(clip[3]))
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
            if constexpr (requires {
                              left->representationId;
                              right->representationId;
                          })
            {
                // Both handles are anchored during this check. A copied or
                // forged representation key is not the same immutable instance.
                return InstanceFramePins::Identity(*left) == InstanceFramePins::Identity(*right) &&
                       std::addressof(*left) == std::addressof(*right);
            }
            else
            {
                return left->assetId == right->assetId && left->generation == right->generation &&
                       left->cooked.product.program.semanticKey == right->cooked.product.program.semanticKey;
            }
        }

        constexpr char PreparationAdmissionError[] =
            "METEOR Scene program preparation exceeds its 64-request admission budget.";
        bool Fail(std::string& error, std::string message)
        {
            error = std::move(message);
            return false;
        }

        // 프레임 결과는 candidate가, 임시 chunk 용량은 SceneHost가 소유한다.
        // 작업 핸들은 호출 스코프에 남겨 조기 실패에도 빌린 저장소보다 먼저 배출한다.
        struct SceneCpuPreparation final
        {
            struct Chunk
            {
                std::size_t input{}, chunk{};
                SceneGeometryIdentity identity;
                math::matrix4x4 world, previousWorld;
                std::array<math::vector4, 3> worldColumns, previousWorldColumns;
                std::uint32_t owner{};
            };

          private:
            ce::render_arena_vector<std::pair<std::size_t, std::size_t>>& m_ranges;
            ce::render_arena_vector<Chunk>& m_chunks;
            ce::render_arena_vector<SceneHost::ForwardDraw>& m_blended;
            job_handle m_work;

          public:
            SceneCpuPreparation(ce::render_arena_vector<std::pair<std::size_t, std::size_t>>& ranges,
                                ce::render_arena_vector<Chunk>& chunks,
                                ce::render_arena_vector<SceneHost::ForwardDraw>& blended)
                : m_ranges(ranges), m_chunks(chunks), m_blended(blended)
            {
            }
            SceneCpuPreparation(const SceneCpuPreparation&) = delete;
            SceneCpuPreparation& operator=(const SceneCpuPreparation&) = delete;

            ~SceneCpuPreparation()
            {
                // An early owner-side failure cannot release storage still used by a
                // worker. Preserve the original error after draining accepted work.
                if (m_work.valid())
                {
                    try
                    {
                        m_work.wait();
                    }
                    catch (...)
                    {
                    }
                }
            }
            bool Join(std::string& error)
            {
                try
                {
                    if (m_work.valid())
                    {
                        m_work.wait();
                    }
                    m_work = {};
                    return true;
                }
                catch (const std::exception& failure)
                {
                    m_work = {};
                    return Fail(error, failure.what());
                }
            }
            void Start(job_scheduler& scheduler, const own::shared_owner<const SceneViewInput>& input)
            {
                m_ranges.resize(input->Draws().size());
                m_blended.clear();
                std::size_t total{};
                for (std::size_t i = 0; i < m_ranges.size(); ++i)
                {
                    const auto count = input->Draws()[i].geometry->Chunks().size();
                    if (count > m_chunks.max_size() - total)
                    {
                        throw std::runtime_error("METEOR CPU preparation exceeds chunk storage capacity.");
                    }
                    m_ranges[i] = {total, count};
                    total += count;
                }
                if (total != input->Cost().chunks)
                {
                    throw std::runtime_error("METEOR sealed chunk cost does not match its immutable ranges.");
                }
                m_chunks.resize(total);
                for (std::size_t i = 0; i < m_ranges.size(); ++i)
                {
                    for (std::size_t j = 0; j < m_ranges[i].second; ++j)
                    {
                        auto& chunk = m_chunks[m_ranges[i].first + j];
                        chunk.input = i;
                        chunk.chunk = j;
                    }
                }
                const auto slices = std::clamp((total + 127) / 128, std::size_t{1}, std::size_t{4});
                const auto prepare = [this, input, slices](std::size_t task)
                {
                    if (task == slices)
                    {
                        for (std::size_t i = 0; i < input->Draws().size(); ++i)
                        {
                            const auto& draw = input->Draws()[i];
                            const auto& program = draw.material->generation->cooked.product.program;
                            if (program.surface &&
                                (draw.queue == SceneCoverage::Blended || (program.features & 0x0800u)))
                            {
                                if (!std::isfinite(draw.viewDepth))
                                {
                                    throw std::runtime_error("METEOR blended sorting depth is non-finite.");
                                }
                                if (m_blended.empty())
                                {
                                    // 불투명 뷰에는 정렬 결과 저장소 자체가 필요하지 않다.
                                    m_blended.reserve(input->Draws().size());
                                }
                                m_blended.push_back({i, draw.geometryKey, draw.viewDepth});
                            }
                        }
                        // 마지막 키인 입력 인덱스가 유일하므로 안정 정렬용 임시 할당은 필요 없다.
                        std::sort(m_blended.begin(), m_blended.end(),
                                  [](const auto& left, const auto& right)
                                  {
                                      if (left.depth != right.depth)
                                      {
                                          return left.depth > right.depth;
                                      }
                                      if (left.geometryKey != right.geometryKey)
                                      {
                                          return left.geometryKey < right.geometryKey;
                                      }
                                      return left.index < right.index;
                                  });
                        return;
                    }
                    const auto first = m_chunks.size() * task / slices;
                    const auto last = m_chunks.size() * (task + 1) / slices;
                    for (auto index = first; index < last; ++index)
                    {
                        auto& result = m_chunks[index];
                        const auto& draw = input->Draws()[result.input];
                        const auto& current = draw.geometry->Chunks()[result.chunk].input;
                        const auto& previous =
                            draw.previousGeometry && draw.previousGeometry.operator->() != draw.geometry.operator->() &&
                                    result.chunk < draw.previousGeometry->Chunks().size()
                                ? draw.previousGeometry->Chunks()[result.chunk].input
                                : current;
                        result.owner = 0x80000001u + static_cast<std::uint32_t>(result.input);
                        result.identity = {draw.geometryKey,
                                           draw.temporalObjectId,
                                           draw.temporalIncarnation,
                                           draw.temporalInstanceId,
                                           draw.materialSlot,
                                           draw.geometry->Source()->Hierarchy().groupCount
                                               ? UINT32_MAX
                                               : draw.geometry->Source()->Geometry().sourceLodIndex,
                                           draw.geometry->Source()->SourcePrimitiveCount()};
                        result.world = math::transpose(current->World());
                        result.previousWorld = math::transpose(previous->World());
                        for (unsigned column = 0; column < 3; ++column)
                        {
                            const auto& world = current->World();
                            const auto& old = previous->World();
                            result.worldColumns[column] = math::vector4(world.m[0][column], world.m[1][column],
                                                                        world.m[2][column], world.m[3][column]);
                            result.previousWorldColumns[column] =
                                math::vector4(old.m[0][column], old.m[1][column], old.m[2][column], old.m[3][column]);
                        }
                    }
                };
                // A nested prepare never blocks a pool worker on another pool job.
                // Small views are cheaper inline; larger views use one bounded batch.
                if (total < 128 || thread_pool::is_worker_thread() || !scheduler.is_running())
                {
                    for (std::size_t task = 0; task <= slices; ++task)
                    {
                        prepare(task);
                    }
                }
                else
                {
                    m_work = scheduler.submit_indexed(slices + 1, prepare);
                }
            }
        };

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
            // Object-space vertex streams: current and previous chunk object-to-world.
            math::matrix4x4 world, previousWorld;
        };
        static_assert(sizeof(SceneConstants) == 4800);
        static_assert(offsetof(SceneConstants, lights) == 368);

        struct SceneShadowConstants
        {
            math::matrix4x4 viewProjection;
            std::uint32_t coverage{};
            float cutoff{};
            std::uint32_t padding0{}, padding1{};
            SceneGeometryIdentity geometryIdentity;
            math::matrix4x4 world;
        };
        static_assert(sizeof(SceneShadowConstants) == 208);

        struct SceneIndirectRootLayout
        {
            // 루트 상한은 그림자 6개, 표면 7개, 머티리얼 타일 6개, Temporal 8개다.
            // 현재 주소 검사는 모두 유지하고 명령별 슬롯 임시 저장소만 스택에 둔다.
            static constexpr std::size_t kCapacity = 8;
            std::array<std::uint32_t, kCapacity> slots{};
            std::size_t count{};

            bool Append(std::uint32_t slot, bool cbv)
            {
                if (count == slots.size())
                {
                    return false;
                }
                slots[count++] = slot | (cbv ? 0x80000000u : 0u);
                return true;
            }

            bool MatchOrInitialize(std::vector<std::uint32_t>& bucket) const
            {
                const std::span<const std::uint32_t> roots{slots.data(), count};
                if (bucket.empty())
                {
                    bucket.assign(roots.begin(), roots.end());
                }
                return std::ranges::equal(bucket, roots);
            }
        };

        template<typename Words, typename Records>
        bool ReserveSceneIndirectCommands(std::size_t count, Words& words, Records& records, std::string& error)
        {
            constexpr auto wordsPerCommand = SceneIndirectRootLayout::kCapacity * 2;
            if (count > words.max_size() / wordsPerCommand || count > records.max_size())
            {
                return Fail(error, "METEOR indirect command preparation exceeds CPU storage capacity.");
            }
            words.reserve(count * wordsPerCommand);
            records.reserve(count);
            return true;
        }

        std::array<RGHandle, 5> Colors(const SceneSurfaceTargets::Outputs& outputs)
        {
            return {outputs.diffuse, outputs.metalRough, outputs.normal, outputs.emissive, outputs.bitmask};
        }

        // Stage contracts travel as blackboard handles. A consumer must receive
        // exactly the version its producer published (both invalid for an empty stage).
        void RequireStage(RGHandle actual, RGHandle expected, const char* message)
        {
            const bool same =
                actual.IsValid() == expected.IsValid() &&
                (!actual.IsValid() || (actual.index == expected.index && actual.version == expected.version &&
                                       actual.kind == expected.kind && actual.epoch == expected.epoch));
            if (!same)
            {
                throw std::runtime_error(message);
            }
        }
        // METEOR declares versioned producers only. Every public stage entry checks
        // this once, so stage code carries no other scheduling-mode branches.
        EnhancedRenderGraph& RequireVersionedGraph(EnhancedRenderGraph& graph)
        {
            if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
            {
                throw std::runtime_error("METEOR Scene requires an ExplicitVersioned render graph.");
            }
            return graph;
        }
        RGHandle BlendedCommandInput(const EnhancedRenderGraph& graph, const IndirectMaterialBatch& batch,
                                     const SceneLayerWork& commands)
        {
            const auto expected = batch.GraphOutput(graph);
            const auto found =
                std::ranges::find_if(commands.blended,
                                     [&](RGHandle input)
                                     {
                                         return input.index == expected.index && input.version == expected.version &&
                                                input.kind == expected.kind && input.epoch == expected.epoch;
                                     });
            if (found == commands.blended.end())
            {
                throw std::runtime_error("METEOR blended consumer has no command input.");
            }
            return *found;
        }
    } // namespace

    struct SceneHost::Program
    {
        struct PacketRoots
        {
            uint32_t resources{}, selector{}, vertices{}, topology{};
        };
        own::shared_owner<const Generation> generation;
        RHIShaderBinary backend{};
        PassLayout layout;
        own::shared_owner<const PassLayout> layoutSnapshot;
        PassLayout shadowLayout;
        LX::Runtime::GraphicsPipeline shadow;
        std::array<LX::Runtime::GraphicsPipeline, 2> gbuffer, visibilitySurface, color, blendedColor, temporal;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> lookup, blendedLookup;
        std::array<LX::Runtime::GraphicsPipeline, 2> subsurface;
        std::array<LX::Runtime::GraphicsPipeline, 2> refraction;
        std::array<std::array<LX::Runtime::GraphicsPipeline, 2>, 2> runtimeEffects;
        PassLayout volumeLayout;
        own::shared_owner<const PassLayout> volumeLayoutSnapshot;
        LX::Runtime::ComputePipeline volume;
        RHIShaderBlob meshShader, visibilityMeshShader, reconstructShader, shadowMeshShader;
        RHIShaderBlob m_cachedSurfaceShader, m_cachedTemporalShader;
        std::unordered_map<uint32_t, RHIPipelineHandle> reconstructionPipelines;
        std::map<uint64_t, RHIPipelineHandle> meshPipelines;
        uint32_t rasterRouteRoot{}, visibilityKeyRoot{}, meshletRoot{}, temporalPreviousRoot{}, geometryTableRoot{},
            visibilityRoot{}, shadowMeshletRoot{2};
        // Host slots recorded while building the root layout below; never recomputed from flags.
        uint32_t decalRoot{}, forwardLightsRoot{}, visibleOwnerRoot{}, specialEffectsRoot{};
        uint32_t shadowViewRoot{5};
        PacketRoots packetRoots, shadowPacketRoots;
        bool meshEnabled{};
        bool hasSurface{}, hasVolume{};
        bool editorPreview{};
        bool hasSubsurface{}, hasTransmission{}, hasSpecial{};
    };

    // Immutable depth allocation. Only accepted/completed recordings publish it.
    // In-flight readers/writers retain this owner independently of the lookup map.
    struct SceneHost::PreparationScratch
    {
        ce::RenderArenaPool m_pool;
        IndirectMaterialBatchPool m_batches;
        // camera 2개와 shadow 최대 9개의 writer가 각자 한 vector를 재사용한다.
        // packet 입력은 UploadPackets 안에서 복사되고 이 저장소를 frame에 게시하지 않는다.
        struct CasterPlan
        {
            struct Key
            {
                own::weak_owner<const MeshSurfaceInput> m_topology;
                own::weak_owner<const Program> m_program;
                size_t m_chunks{};
                uint32_t m_flags{};
                SceneCoverage m_queue{};
                bool m_shadows{};
            };
            struct Entry
            {
                size_t m_input{}, m_chunk{}, m_output{};
                bool m_blended{}, m_doubleSided{}, m_shadowEnabled{};
            };
            std::vector<Key> m_keys;
            std::vector<Entry> m_entries;
            bool m_valid{};
            bool Prepare(const SceneViewInput& input, std::span<const own::shared_owner<const Program>> programs,
                         bool shadows)
            {
                const auto draws = input.Draws();
                bool matches = m_valid && m_keys.size() == draws.size();
                for (size_t i = 0; matches && i < draws.size(); ++i)
                {
                    const auto topology = draws[i].geometry->SceneTopologyIdentity();

                    matches = own::identity(topology) != own::object_id{} &&
                              own::identity(m_keys[i].m_topology) == own::identity(topology) &&
                              own::identity(m_keys[i].m_program) == own::identity(programs[i]) &&
                              m_keys[i].m_chunks == draws[i].geometry->Chunks().size() &&
                              m_keys[i].m_flags == draws[i].coverage.flags && m_keys[i].m_queue == draws[i].queue &&
                              m_keys[i].m_shadows == shadows;
                }
                if (matches)
                {
                    return true;
                }
                m_valid = false;
                m_keys.resize(draws.size());
                m_entries.clear();
                for (size_t i = 0; i < draws.size(); ++i)
                {
                    const auto& draw = draws[i];
                    const auto& program = programs[i];
                    const auto count = draw.geometry->Chunks().size();
                    m_keys[i] = {draw.geometry->SceneTopologyIdentity(),
                                 program,
                                 count,
                                 draw.coverage.flags,
                                 draw.queue,
                                 shadows};
                    for (size_t j = 0; j < count; ++j)
                    {
                        m_entries.push_back(
                            {i, j, m_entries.size(),
                             program->hasSurface && (draw.queue == SceneCoverage::Blended || program->hasTransmission),
                             (draw.coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0,
                             program->hasSurface && !program->editorPreview && shadows && draw.queue != SceneCoverage::Blended});
                    }
                }
                m_valid = true;
                return false;
            }
        };
        struct PacketView
        {
            uint64_t m_view{}, m_epoch{};
            CasterPlan m_casters;
            std::array<ScenePacketGroups, 3> m_groups;
            std::array<ScenePacketRecords, 11> m_records;
            std::vector<ScenePacketGroups::Input> m_inputs;
        };
        std::array<PacketView, 4> m_packetViews;
        size_t m_nextPacketView{};
        PacketView& Packets(uint64_t view, uint64_t epoch)
        {
            for (auto& entry : m_packetViews)
            {
                if (entry.m_view == view && entry.m_epoch == epoch)
                {
                    return entry;
                }
            }
            auto& entry = m_packetViews[m_nextPacketView++ % m_packetViews.size()];
            entry = {};
            entry.m_view = view;
            entry.m_epoch = epoch;
            return entry;
        }
        struct Work
        {
            // The lease precedes all containers: destruction destroys their
            // objects/proxies before the last lease resets retained raw blocks.
            ce::RenderArenaLease m_storage;
            ce::render_arena_vector<SceneCpuPreparation::Chunk> m_chunks;
            ce::render_arena_vector<own::shared_owner<const MeshSurfaceBatch>> m_cameraGeometry, m_blendedGeometry;
            explicit Work(const ce::RenderArenaPool& pool)
                : m_storage(pool.Acquire()), m_chunks(&m_storage->Resource(0)),
                  m_cameraGeometry(&m_storage->Resource(1)), m_blendedGeometry(&m_storage->Resource(1))
            {
            }
        };
    };

    struct SceneHost::ShadowCache
    {
        IRenderDeviceServices* device{};
        std::array<RHITextureHandle, 3> depths{};
        std::vector<std::byte> key;
        std::vector<own::weak_owner<const MeshSurfaceInput>> geometryIdentities;
        std::uint64_t frameId{}, completion{}, bytes{};
        std::uint32_t extent{};
        std::atomic<unsigned> recordedMask{};
        bool MatchesGeometry(std::span<const own::weak_owner<const MeshSurfaceInput>> identities) const
        {
            if (geometryIdentities.size() != identities.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < identities.size(); ++index)
            {
                const auto previous = geometryIdentities[index].lock();
                const auto current = identities[index].lock();
                if (!previous || previous.operator->() != current.operator->())
                {
                    return false;
                }
            }
            return true;
        }
        ~ShadowCache()
        {
            if (device)
            {
                for (const auto depth : depths)
                {
                    if (depth.IsValid())
                    {
                        device->ReleaseTexture(depth);
                    }
                }
            }
        }
    };

#include "MaterialGraphSceneStages.inl"

    struct SceneHost::RasterResources
    {
        own::shared_owner<const SceneFrameGeometry> geometry;
        own::shared_owner<const GpuGeometryVisibility::Frame> first, opaque, layers;
        own::shared_owner<const IndirectMaterialBatch> layerCommands;
        own::shared_owner<const SceneMaterialWork> material;
    };

    // Sealed draw/device inputs and immutable GPU binding references.
    // Base-owner conversion retains the derived Frame, including stage receivers.
    // Only recording identity and execution observations mutate in this shared input.
    struct SceneHost::FrameInput
    {
        static ce::RenderArenaLease AcquireStorage()
        {
            static ce::RenderArenaPool pool;
            return pool.Acquire();
        }
        ce::RenderArenaLease m_cpuStorage{AcquireStorage()};
        struct Draw
        {
            own::shared_owner<const Program> program;
            own::shared_owner<const RenderBindings> bindings;
            bool shadowEnabled{};
            own::shared_owner<const MeshSurfaceBatch> geometry, previousGeometry;
            RHIBufferSlice constants, referenceConstants;
            RHIPipelineHandle m_cachedSurfacePipeline, m_cachedTemporalPipeline;
            RHIBufferSlice visibleOwner;
            // Bin in the camera list (opaque) or the ordered blended list.
            std::uint32_t cameraMeshletBin{UINT32_MAX};
            std::uint32_t materialBucket{UINT32_MAX}, materialTileBucket{UINT32_MAX}, geometryRecord{};
            std::uint32_t indexedBucket{UINT32_MAX};
            bool bucketRepresentative{true};

            std::array<RHIBufferSlice, 3> shadowConstants;
            std::array<own::shared_owner<const GpuGeometryVisibility::Frame>, 3> shadowMeshlets;
            std::uint32_t shadowMeshletBin{UINT32_MAX};

            bool doubleSided{}, blended{}, staticShadow{};
            std::size_t inputIndex{};
        };
        own::shared_owner<const RasterResources> raster;
        own::shared_owner<const SceneGpuData> sceneData;
        own::shared_owner<const ScenePrimitiveCacheFrame> m_primitiveCache;
        std::map<const IndirectMaterialBatch*, RHIBindingTable> packetBindings;
        RHIBindingTable materialPacketBindings, legacyPacketBindings;
        IRenderDeviceServices* device{};
        std::uint64_t recording{}, descriptors{};
        own::shared_owner<const SceneViewInput> input;
        // Occluder: first culling list -> HZB source depth. Final: recovered union -> surface.
        ce::render_arena_vector<Draw> draws{&m_cpuStorage->Resource(0)};
        ce::render_arena_vector<Draw> shadowDraws{&m_cpuStorage->Resource(1)};
        // Static/dynamic caster reads are invariant across this sealed frame's
        // cascades. CheckCurrent rejects reuse with another graph or epoch.
        mutable std::array<ce::render_arena_buffer<EnhancedRenderGraph::RGPassUsage>, 2> m_shadowReadSets;
        mutable std::array<bool, 2> m_shadowReadSetsReady{};
        own::shared_owner<const PassReadPlan> m_passReadPlan;
        void AddPassReadPlan(EnhancedRenderGraph& graph, auto& uses, PassReadPlan::Pass pass,
                             RHIResourceState geometryState) const
        {
            ce::profile_scope profile{ce::marker<"MaterialPassReadPlanResolve">()};
            const auto& inputs = pass == PassReadPlan::Pass::Surface ? draws : shadowDraws;
            if (!m_passReadPlan)
            {
                for (const auto& draw : inputs)
                {
                    const bool selected =
                        pass == PassReadPlan::Pass::Surface
                            ? draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended
                            : draw.shadowEnabled && draw.staticShadow == (pass == PassReadPlan::Pass::StaticShadow);
                    if (!selected)
                    {
                        continue;
                    }
                    uses.push_back({draw.geometry->GraphOutput(graph), geometryState, RGAccessMode::Read});
                    uses.push_back({draw.geometry->GraphMeshlets(graph), geometryState, RGAccessMode::Read});
                    for (const auto& texture : draw.bindings->resources.textures)
                    {
                        uses.push_back({graph.FindImportedTexture(texture.resource),
                                        RHIResourceState::PixelShaderResource, RGAccessMode::Read});
                    }
                }
                return;
            }
            const auto& reads = m_passReadPlan->Get(pass);
            const auto& usageTemplate =
                reads.usageTemplates[geometryState == RHIResourceState::PixelShaderResource ? 1 : 0];
            const size_t first = uses.size();
            uses.resize(first + usageTemplate.size());
            if (!usageTemplate.empty())
            {
                std::memcpy(uses.data() + first, usageTemplate.data(),
                            usageTemplate.size() * sizeof(EnhancedRenderGraph::RGPassUsage));
            }
            size_t slot = first;
            for (const auto index : reads.geometry)
            {
                const auto& geometry = inputs[index].geometry;
                uses[slot++].handle = geometry->GraphOutput(graph);
                uses[slot++].handle = geometry->GraphMeshlets(graph);
            }
            for (const auto texture : reads.textures)
            {
                uses[slot++].handle = graph.FindImportedTexture(texture);
            }
        }
        ce::render_arena_vector<std::pair<size_t, size_t>> inputRanges{&m_cpuStorage->Resource(2)};
        ce::render_arena_vector<ForwardDraw> blendedOrder{&m_cpuStorage->Resource(2)};
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
            return DrawsFor(inputIndex) | std::views::filter([allCommands](const Draw& draw)
                                                             { return allCommands || draw.bucketRepresentative; });
        }
        mutable std::atomic<uint32_t> indexedBucketSubmissions{}, indexedLogicalCommands{};
        mutable std::atomic<uint32_t> temporalBucketSubmissions{};
        mutable std::atomic<uint32_t> blendedBucketSubmissions{};

        RHIBindingTable emptyVisibility;
        RHITextureHandle environment;
        bool shadow{};
        std::uint32_t shadowExtent{};
        struct alignas(64) ShadowRecordingStats
        {
            uint32_t commands{};
            uint32_t buckets{};
        };
        static constexpr uint32_t kMaxRasterRecordingSlices = 4;
        static uint32_t RasterRecordingSlices(uint32_t bucketCount)
        {
            // 작은 버킷 묶음을 쪼개면 encoder 설정과 recording unit만 늘어난다.
            // 기존 전체 병렬 진입 기준을 조각당 최소 배치 수로 보수적으로 재사용한다.
            // 최적 교차점의 실측값은 아니며, 다른 패스와의 병렬 기록은 유지한다.
            const auto slices = bucketCount / EnhancedRenderGraph::kParallelRecordCostThreshold;
            return std::clamp(slices, 1u, kMaxRasterRecordingSlices);
        }
        // Each split callback owns one cache line. Readers reduce only after the
        // graph joins; static-cache and final dynamic raster keep separate slots.
        mutable std::array<std::array<ShadowRecordingStats, kMaxRasterRecordingSlices>, 6> shadowRecordingStats{};
        // Final visibility writers own separate vectors; observations are assembled
        // after recording in slice order. No worker mutates another worker's vector.
        mutable std::array<std::vector<SceneGeometryRouteAudit::Draw>, kMaxRasterRecordingSlices>
            recordedGeometryRoutes;
        mutable bool geometryRouteAuditEnabled{};
        mutable const EnhancedRenderGraph* graph{};
        mutable std::uint64_t graphEpoch{};

        bool UsesMeshlets(const Draw& draw) const
        {
            const auto count = draw.geometry->MeshletCount();
            // Visibility and the ordered replay path share this 2D dispatch ABI.
            // Packet buckets may add slices, but cannot relax ordered replay limits.
            const auto width = (std::min)(count, 65535u);
            const auto height = static_cast<uint32_t>((uint64_t(count) + 65534u) / 65535u);
            return (draw.program->hasSurface || draw.program->hasSpecial) && draw.program->meshEnabled && count > 0 &&
                   device->GetMeshShaderCapabilities().SupportsDispatch(width, height, 1);
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

        // Geometry replay is exclusively for ordered layers. Opaque work is
        // restored from visibility/material tiles; its HW commands stay in Visibility.
        template<class Predicate>
        void AddLayerReads(EnhancedRenderGraph& graph, auto& uses, const Predicate& selected, RGHandle commands) const
        {
            bool any = false;
            for (const auto& draw : draws)
            {
                if (selected(draw))
                {
                    if (!draw.blended || !UsesMeshlets(draw))
                    {
                        throw std::runtime_error("METEOR layer replay requires ordered cooked meshlets.");
                    }
                    any = true;
                }
            }
            if (any)
            {
                raster->layers->AddVisibleIdsRead(graph, uses);
                raster->layerCommands->AddRead(graph, uses, commands);
                sceneData->AddRead(graph, uses);
            }
        }

        void AddReconstructionReads(EnhancedRenderGraph& graph, auto& uses, RGHandle visibilityTarget,
                                    const std::array<RGHandle, 2>& materialInputs, bool primitiveCache = false) const
        {
            uses.push_back({visibilityTarget, RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            uses.push_back(
                {raster->geometry->GraphTable(graph), RHIResourceState::PixelShaderResource, RGAccessMode::Read});
            if (raster->material)
            {
                raster->material->AddRead(graph, uses, materialInputs);
            }
            sceneData->AddRead(graph, uses);
            if (primitiveCache && m_primitiveCache)
            {
                m_primitiveCache->AddRead(graph, uses);
            }
        }
        RHIBindingTable CreateReconstructionVisibility(const EnhancedRenderGraph::ExecuteContext& execution,
                                                       RGHandle visibilityTarget) const
        {
            const auto desc = RHIBindingDesc::Srv2D(execution.ResolveHandle(visibilityTarget), RHIFormat::RG32Uint);
            const auto visibility = device->CreateBindings({&desc, 1});
            if (!visibility.IsValid())
            {
                throw std::runtime_error("METEOR reconstruction visibility target binding failed.");
            }
            return visibility;
        }

        void BindReconstruction(RHIEncoder& encoder, const Draw& draw, RHIBindingTable visibility,
                                RHIPipelineHandle source) const
        {
            auto pipeline = draw.program->reconstructionPipelines.at(source.id);
            if (m_primitiveCache)
            {
                if (source == draw.program->gbuffer[draw.doubleSided].GetHandle())
                {
                    pipeline = draw.m_cachedSurfacePipeline;
                }
                else if (source == draw.program->temporal[draw.doubleSided].GetHandle())
                {
                    pipeline = draw.m_cachedTemporalPipeline;
                }
            }
            if (!pipeline.IsValid())
            {
                throw std::runtime_error("METEOR reconstruction requires the frame's prepared shader permutation.");
            }
            encoder.SetPipeline(RHIBindPoint::Graphics, pipeline);
            encoder.SetBindings(RHIBindPoint::Graphics, draw.program->visibilityRoot, visibility);
            BindPacketResources(encoder, draw, materialPacketBindings, draw.program->packetRoots);
        }
        void DrawReconstruction(RHIEncoder& encoder, const Draw& draw) const
        {
            if (!raster->material)
            {
                throw std::runtime_error("METEOR material tile producer is missing.");
            }
            raster->material->Draw(encoder, draw.materialTileBucket, draw.program->packetRoots.selector);
        }

        void BindPacketResources(RHIEncoder& encoder, const Draw& draw, RHIBindingTable resources,
                                 const Program::PacketRoots& roots, std::optional<unsigned> cascade = {}) const
        {
            BindPacketDescriptors(encoder, draw, resources, roots);
            // Pixel entry points also serve ordered record-zero draws. Keep their
            // legacy descriptors valid once per bucket; packet lanes take t170-173.
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            if (cascade)
            {
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, sceneData->shadowConstants[*cascade]);
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, draw.program->shadowViewRoot,
                                          sceneData->viewConstants);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, draw.geometry->Meshlets());
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, sceneData->recordBuffer->Slice());
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 4, RHIBufferSlice::Whole(raster->geometry->buffer));
            }
            else
            {
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, sceneData->viewConstants);
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->geometryTableRoot,
                                      RHIBufferSlice::Whole(raster->geometry->buffer));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->visibleOwnerRoot,
                                      sceneData->recordBuffer->Slice());
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->meshletRoot, draw.geometry->Meshlets());
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->temporalPreviousRoot,
                                      RHIBufferSlice::Whole(draw.previousGeometry->Buffer()));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->temporalPreviousRoot + 1,
                                      sceneData->recordBuffer->Slice());
            }
        }
        void BindPacketDescriptors(RHIEncoder& encoder, const Draw& draw, RHIBindingTable resources,
                                   const Program::PacketRoots& roots) const
        {
            const auto page = sceneData->recordPages.at(draw.geometryRecord);
            if (page >= sceneData->pages.size() || !resources.IsValid())
            {
                throw std::runtime_error("METEOR packet bucket has no retained geometry page/resources.");
            }
            const auto& bindings = sceneData->pages[page];
            encoder.SetBindings(RHIBindPoint::Graphics, roots.resources, resources);
            encoder.SetBindings(RHIBindPoint::Graphics, roots.vertices, bindings.vertexTable);
            encoder.SetBindings(RHIBindPoint::Graphics, roots.topology, bindings.topologyTable);
        }

        void BindLayerGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            if (!draw.blended)
            {
                throw std::runtime_error("METEOR opaque geometry must use visibility restoration.");
            }
            BindPacketDescriptors(encoder, draw, legacyPacketBindings, draw.program->packetRoots);
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, draw.program->packetRoots.selector,
                                      sceneData->viewConstants);
            encoder.SetBindings(RHIBindPoint::Graphics, draw.program->visibilityRoot, emptyVisibility);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->geometryTableRoot,
                                  RHIBufferSlice::Whole(raster->geometry->buffer));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(draw.geometry->Buffer()));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->visibleOwnerRoot, draw.visibleOwner);
            const auto& list = raster->layers;
            encoder.SetRootBuffer(
                RHIBindPoint::Graphics, draw.program->temporalPreviousRoot + 1,
                list->VisibleIds(list->MeshletOutputOffset(draw.cameraMeshletBin), draw.geometry->MeshletCount() + 1));
        }

        void DrawLayerGeometry(RHIEncoder& encoder, const Draw& draw) const
        {
            if (!draw.blended || !UsesMeshlets(draw) || !raster->layerCommands || draw.indexedBucket == UINT32_MAX)
            {
                throw std::runtime_error("METEOR layer replay requires its ordered meshlet command bucket.");
            }
            raster->layerCommands->Draw(encoder, draw.indexedBucket);
            indexedBucketSubmissions.fetch_add(1, std::memory_order_relaxed);
            indexedLogicalCommands.fetch_add(raster->layerCommands->buckets[draw.indexedBucket].count,
                                             std::memory_order_relaxed);
        }

        void CheckCurrent(const EnhancedRenderGraph* currentGraph = nullptr) const
        {
            if (device->GetCurrentUploadRecordingId() != recording ||
                device->GetDescriptorVersionToken() != descriptors)
            {
                throw std::runtime_error("METEOR Scene frame has stale upload/descriptor ownership.");
            }
            if (currentGraph &&
                (graph != currentGraph->DeclarationGraph() || currentGraph->DeclarationEpoch() != graphEpoch))
            {
                throw std::runtime_error("METEOR Scene frame belongs to a reset graph.");
            }
        }
    };

    struct SceneHost::Frame : FrameInput
    {
        GeometryStage geometryStage;
        FirstCullStage firstCullStage;
        OccluderStage occluderStage;
        FinalCullStage finalCullStage;
        ShadowCacheStage shadowCacheStage;
        ShadowFirstCullStage shadowFirstCullStage;
        ShadowOccluderStage shadowOccluderStage;
        ShadowFinalCullStage shadowFinalCullStage;
        ShadowRasterStage shadowRasterStage;
        ShadowHistoryStage shadowHistoryStage;
        TemporalStage temporalStage;
        VisibilityStage visibilityStage;
        MaterialWorkStage materialWorkStage;
        SurfaceResolveStage surfaceResolveStage;
        HistoryStage historyStage;
        EffectStage effectStage;
    };

#include "MaterialGraphSceneGeometryStage.inl"
#include "MaterialGraphSceneFirstCullStage.inl"
#include "MaterialGraphSceneOccluderStage.inl"
#include "MaterialGraphSceneFinalCullStage.inl"
#include "MaterialGraphSceneShadowStage.inl"
#include "MaterialGraphSceneTemporalStage.inl"
#include "MaterialGraphSceneSurfaceStage.inl"
#include "MaterialGraphSceneHistoryStage.inl"
#include "MaterialGraphSceneLayerStage.inl"
#include "MaterialGraphSceneVolumeStage.inl"
#include "MaterialGraphSceneEffectStage.inl"

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
        std::chrono::steady_clock::time_point started{std::chrono::steady_clock::now()};
        bool nativeLogged{};
        detail::NativeAdmission nativeAdmission;
    };

    struct SceneHost::Slot
    {
        own::shared_owner<const Instance> requested, active;
        EnhancedMaterialCoverage requestedCoverage, activeCoverage;
        std::uint64_t revision{};
        std::uint64_t selectedRepresentation{};
    };

    SceneHost::SceneHost(job_scheduler& scheduler) : scheduler_(scheduler) {}

    SceneHost::~SceneHost()
    {
        if (device_)
        {
            device_->UnregisterUploadTransactionListener(this);
        }
    }

    bool SceneHost::IsProgramReady(const own::shared_owner<const Generation>& generation, RHIShaderBinary backend) const
    {
        return std::ranges::any_of(
            programs_, [&](const auto& program)
            { return SamePinnedObject(program->generation, generation) && program->backend == backend; });
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
        std::size_t retainedFailures =
            std::ranges::count_if(preparations_, [](const auto& item) { return !item->error.empty(); });
        std::erase_if(preparations_,
                      [&](const auto& preparation)
                      {
                          if (retainedFailures <= 16 || preparation->error.empty() ||
                              SamePinnedObject(preparation->generation, requested) ||
                              (preparation->job.valid() && !preparation->job.is_complete()))
                          {
                              return false;
                          }
                          const bool referenced = std::ranges::any_of(
                              slots_,
                              [&](const auto& item)
                              {
                                  return (item.second->requested && SamePinnedObject(item.second->requested->generation,
                                                                                     preparation->generation)) ||
                                         (item.second->active &&
                                          SamePinnedObject(item.second->active->generation, preparation->generation));
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
        const auto reject = [&](std::string message)
        {
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
        {
            return reject("METEOR Scene packet ABI requires force reimport/recook; runtime conversion is forbidden.");
        }
        auto work = own::make_shared<Preparation::Work>();
        work->generation = preparation->generation;
        work->backend = backend;
        if (!LoadSceneShaders(product, backend, *work, error))
        {
            return reject(error);
        }
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
        char legacyAdmission[4]{};
        const bool nonblockingAdmission = RHIShaderCompiler::GetOutput()==RHIShaderBinary::Dxil &&
            !(GetEnvironmentVariableA("CE_LEGACY_PSO_ADMISSION",legacyAdmission,sizeof(legacyAdmission))==1 && legacyAdmission[0]=='1');
        for (const auto& preparation : preparations_)
        {
            const bool cooked = preparation->work && preparation->work->cooked;
            const bool prepared = preparation->checked && preparation->program;
            if (!preparation->error.empty() ||
                (!prepared && !cooked && (!preparation->job.valid() || !preparation->job.is_complete())))
            {
                continue;
            }
            const auto reject = [&](std::string message)
            {
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
                const auto describe = [&]() -> bool
                {
                    auto candidate = own::make_shared<Program>();
                    candidate->generation = preparation->generation;
                    candidate->backend = backend;
                    const auto caps = context.resources->GetMeshShaderCapabilities();
                    candidate->meshEnabled = caps.meshShader && caps.meshIndirect && caps.maxOutputVertices >= 64 &&
                                             caps.maxOutputPrimitives >= 126 && caps.maxThreadsPerGroup >= 64 &&
                                             caps.maxThreadGroupSizeX >= 64 && caps.maxOutputMemoryBytes >= 16384;
                    candidate->meshShader = compiled->mesh.bytecode;
                    candidate->visibilityMeshShader = compiled->visibilityMesh.bytecode;
                    candidate->reconstructShader = compiled->reconstructMesh.bytecode;
                    candidate->shadowMeshShader = compiled->shadowMesh.bytecode;
                    candidate->m_cachedSurfaceShader = compiled->m_cachedGbuffer.bytecode;
                    candidate->m_cachedTemporalShader = compiled->m_cachedTemporal.bytecode;
                    const auto& product =
                        compiled->verified ? *compiled->verified : candidate->generation->cooked.product;
                    const auto prepare = [&](LX::Runtime::GraphicsPipeline& request,
                                             const RHIGraphicsPipelineDesc& desc, std::string_view vs,
                                             std::string_view ps)
                    {
                        LX::Runtime::GraphicsShaderDescription shader;
                        return DescribeGraphicsShader(product, backend, vs, ps, shader, error) &&
                               request.Prepare(desc, std::move(shader), error);
                    };
                    candidate->editorPreview = IsEditorPreviewProgram(product.program);
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
                    candidate->decalRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::SrvTable(6, 9, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(3, RHIShaderVisibility::Pixel));
                    candidate->forwardLightsRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(128, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(129, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(130, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(5, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(131, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Srv(132, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::SrvTable(2, 133, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::SrvTable(3, 135, RHIShaderVisibility::Pixel));
                    host.push_back(RHILayout::Cbv(6, RHIShaderVisibility::Pixel));
                    candidate->visibleOwnerRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(138, RHIShaderVisibility::All));
                    if (candidate->hasSpecial)
                    {
                        candidate->specialEffectsRoot = static_cast<uint32_t>(host.size());
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
                    candidate->rasterRouteRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Srv(161));
                    candidate->visibilityKeyRoot = static_cast<uint32_t>(host.size());
                    host.push_back(RHILayout::Uav(1, RHIShaderVisibility::Pixel));
                    const auto appendPacketLayout =
                        [](std::vector<RHIPipelineLayoutParam>& parameters, Program::PacketRoots& roots)
                    {
                        // cache key/data도 같은 SRV table에 포함해 root DWORD를 늘리지 않는다.
                        // geometry 배열 둘과 b8을 합쳐 기존의 다섯 DWORD 계약을 유지한다.
                        roots.resources = static_cast<uint32_t>(parameters.size());
                        parameters.push_back(RHILayout::SrvBufferTable(6, 170, RHIShaderVisibility::All));
                        roots.selector = static_cast<uint32_t>(parameters.size());
                        parameters.push_back(RHILayout::Cbv(8, RHIShaderVisibility::All));
                        roots.vertices = static_cast<uint32_t>(parameters.size());
                        parameters.push_back(
                            RHILayout::SrvBufferArray(SceneGpuData::kGeometryPageSize, 256, RHIShaderVisibility::All));
                        roots.topology = static_cast<uint32_t>(parameters.size());
                        parameters.push_back(
                            RHILayout::SrvBufferArray(SceneGpuData::kGeometryPageSize, 512, RHIShaderVisibility::All));
                    };
                    appendPacketLayout(host, candidate->packetRoots);
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
                    if (candidate->hasSurface && !candidate->editorPreview)
                    {
                        std::vector<RHIPipelineLayoutParam> shadowHost{RHILayout::Cbv(4),
                                                                       RHILayout::Srv(0, RHIShaderVisibility::All),
                                                                       RHILayout::Srv(155),
                                                                       RHILayout::Srv(157),
                                                                       RHILayout::Srv(158, RHIShaderVisibility::All),
                                                                       RHILayout::Cbv(0, RHIShaderVisibility::All)};
                        appendPacketLayout(shadowHost, candidate->shadowPacketRoots);
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
                        std::fill(std::begin(visibilityDesc.rtvFormats), std::end(visibilityDesc.rtvFormats),
                                  RHIFormat::Unknown);
                        if (!prepare(candidate->visibilitySurface[side], visibilityDesc, "LXSceneVS",
                                     "LXSceneVisibilityPS"))
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
                        std::fill(std::begin(temporalDesc.rtvFormats), std::end(temporalDesc.rtvFormats),
                                  RHIFormat::Unknown);
                        temporalDesc.rtvFormats[0] = RHIFormat::RG16Float;
                        std::fill_n(std::begin(temporalDesc.rtvFormats) + 1, 3, RHIFormat::R16Float);
                        if (!prepare(candidate->temporal[side], temporalDesc, "LXSceneTemporalVS", "LXSceneTemporalPS"))
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
                        if (candidate->hasSurface && !candidate->editorPreview)
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
                        for (unsigned part = 0; part < (candidate->editorPreview ? 0u : 2u); ++part)
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

                    candidate->layoutSnapshot = own::make_shared<const PassLayout>(candidate->layout);
                    if (candidate->hasVolume)
                    {
                        candidate->volumeLayoutSnapshot = own::make_shared<const PassLayout>(candidate->volumeLayout);
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
            if (!pollsRemaining && !nonblockingAdmission)
            {
                continue;
            }
            auto& program = *preparation->program;
            std::vector<LX::Runtime::GraphicsPipeline*> requests{&program.temporal[0],
                                                                 &program.temporal[1],
                                                                 &program.visibilitySurface[0],
                                                                 &program.visibilitySurface[1],
                                                                 &program.gbuffer[0],
                                                                 &program.gbuffer[1],
                                                                 &program.color[0],
                                                                 &program.color[1]};
            if (!program.editorPreview)
                for (auto& part : program.lookup) for (auto& side : part) requests.push_back(&side);
            if (program.hasSurface && !program.editorPreview)
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
            if (program.hasSurface && !program.editorPreview)
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
                    if (!preparation->nativeAdmission.CanPoll(request,pollsRemaining,nonblockingAdmission))
                    {
                        if (!nonblockingAdmission) break;
                        continue;
                    }
                    if (request->Poll(*context.psoManager, error) == RHIPipelineRequestState::Failed)
                    {
                        reject(error.empty() ? "METEOR Scene native PSO preparation failed." : error);
                        break;
                    }
                }
            }
            const bool indexedReady=preparation->program &&
                std::ranges::all_of(requests, [](const auto* request) { return request->IsValid(); });
            if (preparation->program && (indexedReady || nonblockingAdmission))
            {
                if (indexedReady && !preparation->nativeLogged && GetEnvironmentVariableA("CE_TRACE_SHADER_COMPILE", nullptr, 0)) {
                    preparation->nativeLogged=true;
                    std::printf("[material.prepare] graph=%s phase=indexed-ready elapsedMs=%.3f\n",
                        Uuid::ToString(preparation->generation->assetId.value).c_str(),
                        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparation->started).count());
                }
                if (program.meshEnabled)
                {
                    for (const auto* request : requests)
                    {
                        if (!request->IsValid()) continue;
                        const bool temporal = request == &program.temporal[0] || request == &program.temporal[1];
                        const bool reconstruct = request != &program.shadow && request != &program.visibilitySurface[0] && request != &program.visibilitySurface[1];
                        const auto indexed = request->GetHandle();
                        if (program.meshPipelines.contains(indexed.id) &&
                            (!reconstruct || program.reconstructionPipelines.contains(indexed.id)))
                        {
                            continue;
                        }
                        const auto& raster = request->GetDesc();
                        const auto& shader =
                            request == &program.shadow ? program.shadowMeshShader
                            : (request == &program.visibilitySurface[0] || request == &program.visibilitySurface[1])
                                ? program.visibilityMeshShader
                                : program.meshShader;
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
                        RHIPipelineHandle pipeline;
                        char synchronous[4]{};
                        const bool sync=RHIShaderCompiler::GetOutput()!=RHIShaderBinary::Dxil || (GetEnvironmentVariableA("CE_SYNC_MESH_PSO",synchronous,sizeof(synchronous))==1 && synchronous[0]=='1');
                        auto state=RHIPipelineRequestState::Ready;
                        if (const auto accepted=program.meshPipelines.find(indexed.id); accepted!=program.meshPipelines.end())
                            pipeline=accepted->second;
                        else state=sync
                            ? ((pipeline=context.psoManager->GetOrCreateMesh(desc,error)).IsValid() ? RHIPipelineRequestState::Ready : RHIPipelineRequestState::Failed)
                            : context.psoManager->RequestMesh(desc,pipeline,error);
                        if (state==RHIPipelineRequestState::Failed)
                        {
                            reject("METEOR Scene meshlet PSO creation failed: " + error);
                            break;
                        }
                        if (state==RHIPipelineRequestState::Ready)
                            program.meshPipelines.emplace(indexed.id, pipeline);
                        if (request != &program.shadow && request != &program.visibilitySurface[0] &&
                            request != &program.visibilitySurface[1])
                        {
                            desc.msBytecode = program.reconstructShader.Data();
                            desc.msSize = program.reconstructShader.Size();
                            desc.depthEnable = temporal ? raster.depthEnable : false;
                            desc.depthWriteMask = temporal ? raster.depthWriteMask : RHIDepthWrite::Zero;
                            if (temporal)
                            {
                                desc.depthFunc = RHICompareOp::Always;
                            }
                            desc.cullMode = RHICullMode::None;
                            RHIPipelineHandle reconstructed;
                            const auto reconstructedState=sync
                                ? ((reconstructed=context.psoManager->GetOrCreateMesh(desc,error)).IsValid() ? RHIPipelineRequestState::Ready : RHIPipelineRequestState::Failed)
                                : context.psoManager->RequestMesh(desc,reconstructed,error);
                            if (reconstructedState==RHIPipelineRequestState::Pending) continue;
                            if (reconstructedState==RHIPipelineRequestState::Failed)
                            {
                                reject("METEOR reconstruction PSO failed: " + error);
                                break;
                            }
                            program.reconstructionPipelines.emplace(indexed.id, reconstructed);
                        }
                        // The compatibility path budgets blocking backend calls; DX12 requests own their inputs.
                        if (sync) break;
                    }
                    if (!preparation->program ||
                        !std::ranges::all_of(
                            requests,
                            [&](const auto* request)
                            {
                                if (!request->IsValid()) return false;
                                const bool reconstruct = request != &program.shadow && request != &program.visibilitySurface[0] && request != &program.visibilitySurface[1];
                                return program.meshPipelines.contains(request->GetHandle().id) &&
                                    (!reconstruct || program.reconstructionPipelines.contains(request->GetHandle().id));
                            }))
                    {
                        continue;
                    }
                }
                if (!indexedReady) continue;
                // Allocate before the converting move. If growth fails, the
                // checked preparation still owns its ready Program for a retry.
                if (programs_.size() == programs_.capacity())
                {
                    programs_.reserve((std::max)(programs_.size() + 1, programs_.capacity() * 2));
                }
                if (GetEnvironmentVariableA("CE_TRACE_SHADER_COMPILE", nullptr, 0))
                    std::printf("[material.prepare] graph=%s phase=ready elapsedMs=%.3f mesh=%zu reconstruction=%zu\n",
                        Uuid::ToString(preparation->generation->assetId.value).c_str(),
                        std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparation->started).count(),
                        program.meshPipelines.size(), program.reconstructionPipelines.size());
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
        const auto sameCoverage = [](const auto& a, const auto& b)
        { return a.flags == b.flags && a.cutoff == b.cutoff && a.baseAlpha == b.baseAlpha; };
        const auto supported = [](const SceneDrawInput& draw)
        {
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
                return Fail(error,
                            "METEOR Scene Material slots must identify one exact instance and coverage per view.");
            }
        }
        std::erase_if(slots_,
                      [&](const auto& item)
                      {
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
            if (!SamePinnedObject(slot->requested, draw->material) ||
                !sameCoverage(slot->requestedCoverage, draw->coverage))
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
        // Only an explicitly marked Editor placement preview may survive while
        // its source program prepares. Other material failures retain strict semantics.
        for (const auto& draw : requested->Draws())
        {
            if (!IsProgramReady(draw.material->generation, backend))
            {
                const auto& slot = slots_.at({view.sceneEpoch, view.viewId, draw.materialSlot});
                if (slot->active && slot->active->editorPreview &&
                    IsProgramReady(slot->active->generation, backend)) continue;
                const auto failed =
                    std::ranges::find_if(preparations_,
                                         [&](const auto& item)
                                         {
                                             return SamePinnedObject(item->generation, draw.material->generation) &&
                                                    item->backend == backend && !item->error.empty();
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
                    std::printf("[meteor.scene.prepare] pending=%zu ready=%zu\n", preparations_.size(),
                                programs_.size());
                    for (const auto& preparation : preparations_)
                    {
                        std::printf("[meteor.scene.prepare] checked=%u cooked=%u native=%u mesh=%zu error=%s\n",
                                    unsigned(preparation->checked),
                                    unsigned(preparation->work && preparation->work->cooked),
                                    unsigned(bool(preparation->program)),
                                    preparation->program ? preparation->program->meshPipelines.size() : 0,
                                    preparation->error.c_str());
                    }
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
            if (!IsProgramReady(draw.material->generation, backend))
            {
                draw.materialPinIndex = selected->materialPins_->Retain(slot->active);
                draw.material = selected->materialPins_->Borrow(draw.materialPinIndex);
                draw.coverage = slot->activeCoverage;
                if (!ClassifySceneCoverage(draw.coverage, draw.queue, error)) return false;
            }
            if (slot->selectedRepresentation != draw.material->representationId)
            {
                slot->selectedRepresentation = draw.material->representationId;
                slot->revision = ++selectionSerial_;
            }
            draw.selectionRevision = slot->revision;
        }
        // Only the cache's CPU record is pruned. Shared native handles stay in the
        // backend cache; global cache eviction remains a separate lifetime policy.
        // This drops only the cache pin. Frames/recordings keep their exact Program;
        // reference counts are not an in-use or GPU-completion decision.
        std::erase_if(
            programs_,
            [&](const auto& program)
            {
                return programs_.size() > 32 &&
                       std::ranges::none_of(
                           slots_,
                           [&](const auto& item)
                           {
                               return (item.second->active &&
                                       SamePinnedObject(item.second->active->generation, program->generation)) ||
                                      (item.second->requested &&
                                       SamePinnedObject(item.second->requested->generation, program->generation));
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
                const auto entry = context.textureCache->GetOrUpload(
                    (texture.owner ? &*texture.owner.borrow() : nullptr),
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
        if (m_preparing.test_and_set(std::memory_order_acquire))
        {
            return Fail(error, "METEOR Scene preparation cannot reenter the same host.");
        }
        struct PreparationScope final
        {
            std::atomic_flag& m_preparing;
            ~PreparationScope() { m_preparing.clear(std::memory_order_release); }
        } preparationScope{m_preparing};
        // 작업 소유자보다 먼저 생성해 도움 실행 중 재진입과 동시 스크래치 재사용을 막는다.
        ce::profile_scope profile{ce::marker<"MaterialGraphScenePrepare">()};
        // Keep sequential phases balanced on every early return as well as success.
        std::optional<ce::profile_scope> preparationPhase;
        const auto nextPreparationPhase = [&](ce::marker_id marker)
        {
            preparationPhase.reset();
            preparationPhase.emplace(marker);
        };
        nextPreparationPhase(ce::marker<"MaterialSceneFrameSetup">());
        if (device_)
        {
            geometry_.BeginFrame(device_->GetCurrentUploadRecordingId());
        }
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
            bufferPool_ = own::make_shared<TransientBufferPool>(*device_);
        }
        if (!sceneResidentBuffers_.Initialize(*device_, bufferPool_, error))
        {
            return false;
        }
        if (!m_preparationScratch)
        {
            m_preparationScratch = own::make_unique<PreparationScratch>();
        }
        PreparationScratch::Work scratch(m_preparationScratch->m_pool);
        auto candidate = own::make_shared<Frame>();
        candidate->device = device_;
        candidate->recording = device_->GetCurrentUploadRecordingId();
        candidate->descriptors = device_->GetDescriptorVersionToken();
        candidate->input = std::move(input);
        SceneCpuPreparation cpuPreparation(candidate->inputRanges, scratch.m_chunks, candidate->blendedOrder);
        cpuPreparation.Start(scheduler_, candidate->input);
        candidate->environment = environment;
        const auto emptyVisibility = RHIBindingDesc::Srv2D({}, RHIFormat::RG32Uint).OrNull();
        candidate->emptyVisibility = device_->CreateBindings({&emptyVisibility, 1});
        if (!candidate->emptyVisibility.IsValid())
        {
            return Fail(error, "METEOR empty layer visibility binding failed.");
        }
        const bool previewOnly = !candidate->input->Draws().empty() && std::ranges::all_of(
            candidate->input->Draws(), [](const auto& draw) { return IsEditorPreviewProgram(draw.material->generation->cooked.product.program); });
        candidate->shadow = shadow.enabled && !previewOnly;
        candidate->shadowExtent = shadow.mapResolution ? shadow.mapResolution : context.width;
        const std::array<std::byte, SceneVolumeConstantsBytes> emptyMedium{};
        candidate->effectStage.volume.constants = device_->UploadConstants(emptyMedium.data(), emptyMedium.size());
        candidate->effectStage.volume.buffer = device_->AllocateUpload({48, RHIUploadUsage::Raw, 16});
        if (!candidate->effectStage.volume.constants.IsValid() || !candidate->effectStage.volume.buffer.IsWritable())
        {
            return Fail(error, "METEOR Scene empty medium allocation failed.");
        }
        std::memset(candidate->effectStage.volume.buffer.cpuAddress, 0, 48);
        const RHIBindingDesc emptyVolumeBindings[]{RHIBindingDesc::SrvArray({}, RHIFormat::R32Float, 3).OrNull(),
                                                   RHIBindingDesc::SrvCube({}, RHIFormat::RGBA16Float, 1).OrNull()};
        candidate->effectStage.volume.table = device_->CreateBindings(emptyVolumeBindings);
        if (!candidate->effectStage.volume.table.IsValid())
        {
            return Fail(error, "METEOR Scene empty medium bindings could not be allocated.");
        }
        nextPreparationPhase(ce::marker<"MaterialSceneEffectsPrepare">());
        const bool hasAlpha = std::ranges::any_of(candidate->input->Draws(),
                                                  [](const auto& draw)
                                                  {
                                                      const auto& p = draw.material->generation->cooked.product.program;
                                                      return p.surface && (draw.queue == SceneCoverage::Blended ||
                                                                           (p.features & 0x0800u) != 0);
                                                  });
        if (!lookup_.Prepare(context, candidate->input->View().viewId, environment, irradiance, prefiltered,
                             environmentGeneration, budget.lookupBytes, candidate->effectStage.lookup.opaque, error,
                             importance, source, false, previewOnly || budget.lookupApproximate, false,
                             previewOnly || budget.lookupRuntimeEvaluation))
        {
            return false;
        }
        if (hasAlpha &&
            !lookup_.Prepare(context, candidate->input->View().viewId, environment, irradiance, prefiltered,
                             environmentGeneration, budget.lookupBytes, candidate->effectStage.lookup.layered, error,
                             importance, source, true, budget.lookupApproximate, false, budget.lookupRuntimeEvaluation))
        {
            return false;
        }
        const bool hasSpecial = std::ranges::any_of(
            candidate->input->Draws(), [](const auto& draw)
            { return (draw.material->generation->cooked.product.program.features & 0x1800u) != 0; });
        if (hasSpecial && !budget.lookupRuntimeEvaluation)
        {
            std::array<RHIBindingDesc, 14> inputs;
            for (auto& input : inputs)
            {
                input = RHIBindingDesc::Srv2D({}, RHIFormat::RGBA32Float).OrNull();
            }
            candidate->effectStage.runtime.emptyInputs = device_->CreateBindings(inputs);
            const RHIBindingDesc background[]{RHIBindingDesc::Srv2D({}, RHIFormat::RGBA16Float).OrNull(),
                                              RHIBindingDesc::Srv2D({}, RHIFormat::R32Float).OrNull()};
            candidate->effectStage.runtime.emptyBackground = device_->CreateBindings(background);
            if (!candidate->effectStage.runtime.emptyInputs.IsValid() ||
                !candidate->effectStage.runtime.emptyBackground.IsValid())
            {
                return Fail(error, "Offline special surface root bindings could not be allocated.");
            }
        }
        if (hasSpecial && !budget.lookupRuntimeEvaluation &&
            !subsurface_.Prepare(context, environment, budget.subsurfaceBytes, candidate->effectStage.subsurface,
                                 error))
        {
            return false;
        }
        const bool hasTransmission = std::ranges::any_of(
            candidate->input->Draws(), [](const auto& draw)
            { return (draw.material->generation->cooked.product.program.features & 0x0800u) != 0; });
        const bool hasVolume =
            std::ranges::any_of(candidate->input->Draws(), [](const auto& draw)
                                { return draw.material->generation->cooked.product.program.volume; });
        if (hasVolume && !volume_.Prepare(context, *candidate->input, environment, shadow, budget.volumeBytes,
                                          candidate->effectStage.volume.frame, error))
        {
            return false;
        }
        if (hasTransmission && !budget.lookupRuntimeEvaluation &&
            !refraction_.Prepare(context, candidate->input->ViewProjection(), environment, budget.refractionBytes,
                                 candidate->effectStage.refraction, error, candidate->effectStage.volume.frame))
        {
            return false;
        }
        if (hasSpecial && budget.lookupRuntimeEvaluation &&
            !runtimeEffects_.Prepare(context, candidate->input->ViewProjection(), hasTransmission,
                                     bool(candidate->effectStage.volume.frame), budget.subsurfaceBytes,
                                     budget.refractionBytes, candidate->effectStage.runtime.frame, error))
        {
            return false;
        }
        for (unsigned enabled = 0; enabled < candidate->effectStage.decal.constants.size(); ++enabled)
        {
            const std::array<std::uint32_t, 4> constants{enabled, 0, 0, 0};
            candidate->effectStage.decal.constants[enabled] =
                device_->UploadConstants(constants.data(), sizeof(constants));
            if (!candidate->effectStage.decal.constants[enabled].IsValid())
            {
                return Fail(error, "METEOR Scene Decal constants allocation failed.");
            }
        }
        if (!cpuPreparation.Join(error))
        {
            return false;
        }
        nextPreparationPhase(ce::marker<"MaterialSceneGeometryTableSetup">());
        auto geometryTable = own::make_shared<SceneFrameGeometry>();
        geometryTable->device = device_;
        geometryTable->pool = bufferPool_;
        auto sceneData = own::make_shared<SceneGpuData>();
        candidate->sceneData = sceneData;

        if (!device_->GetMeshShaderCapabilities().meshIndirect)
        {
            return Fail(error, "METEOR Scene requires GPU indirect work; no CPU/direct surface route.");
        }

        // Seal의 불변 chunk 수와 기존 배경 슬롯을 함께 예약해 마지막 레코드까지 이동을 피한다.
        // 이전 프레임의 용량이나 GPU 완료 상태를 준비 중인 프레임으로 넘기지 않는다.
        const auto chunkCount = candidate->input->Cost().chunks;
        if (chunkCount > candidate->draws.max_size() ||
            chunkCount > geometryTable->records.max_size() - geometryTable->records.size() ||
            chunkCount > geometryTable->geometry.max_size() - geometryTable->geometry.size())
        {
            return Fail(error, "METEOR Scene chunk storage exceeds CPU container capacity.");
        }
        const auto preparedChunks = static_cast<std::size_t>(chunkCount);
        candidate->draws.resize(preparedChunks);
        geometryTable->records.resize(1 + preparedChunks);
        geometryTable->geometry.resize(1 + preparedChunks);
        sceneData->records.resize(1 + preparedChunks);
        sceneData->recordPages.resize(1 + preparedChunks, UINT32_MAX);
        // One producer per exact sealed geometry/pose input within this frame.
        // The owners remain pinned by the input and output arrays during lookup.
        ce::render_arena_vector<std::pair<const MeshSurfaceInput*, own::shared_owner<const MeshSurfaceBatch>>>
            preparedGeometry{&scratch.m_storage->Resource(0)};
        preparedGeometry.reserve(preparedChunks * 2);
        const auto prepareGeometry = [&](const own::shared_owner<const MeshSurfaceInput>& source,
                                         own::shared_owner<const MeshSurfaceBatch>& output)
        {
            if (const auto found = std::ranges::find(
                    preparedGeometry, source.operator->(),
                    &std::pair<const MeshSurfaceInput*, own::shared_owner<const MeshSurfaceBatch>>::first);
                found != preparedGeometry.end())
            {
                output = found->second;
                return true;
            }
            if (!geometry_.Prepare(*device_, source, output, error, true,
                                   candidate->input->View().temporalFrame.realFrameId
                                       ? candidate->input->View().temporalFrame.realFrameId
                                       : candidate->input->View().frameId,
                                   candidate->input->View().viewId, true, true, budget.clusterStreaming))
            {
                return false;
            }
            preparedGeometry.emplace_back(source.operator->(), output);
            return true;
        };

        // 뷰에서 고정된 행렬과 조명 필드는 한 번 구성한다. Prepare가 소유한 작업 상수에서
        // draw별 ID, coverage, owner와 chunk별 현재/이전 world를 빠짐없이 덮어쓴다.
        SceneConstants viewConstants{};
        viewConstants.viewProjection = math::transpose(candidate->input->ViewProjection());
        const auto& eye = candidate->input->Surface().eye;
        viewConstants.eye = math::vector4(eye[0], eye[1], eye[2], 1);
        viewConstants.width = context.width;
        viewConstants.height = context.height;
        const auto& temporal = candidate->input->View().temporalFrame;
        const auto currentView = std::bit_cast<math::matrix4x4>(temporal.camera.viewMatrix);
        const auto currentProjection = std::bit_cast<math::matrix4x4>(temporal.camera.projectionMatrix);
        const auto currentToPrevious = std::bit_cast<math::matrix4x4>(temporal.camera.clipToPreviousClip);
        const auto currentVP =
            temporal.camera.valid ? currentView * currentProjection : candidate->input->ViewProjection();
        viewConstants.temporalCurrentProjection = math::transpose(currentVP);
        viewConstants.temporalPreviousProjection =
            math::transpose(temporal.camera.valid ? currentVP * currentToPrevious : currentVP);
        viewConstants.padding[1] =
            (context.width + EnhancedForwardLighting::TileSize - 1) / EnhancedForwardLighting::TileSize;
        viewConstants.environment = environment.IsValid();
        viewConstants.hasShadow = shadow.enabled;
        viewConstants.shadowBlend = shadow.cascadeBlendBand;
        viewConstants.shadowSplits = shadow.splitDepths;
        viewConstants.shadowBias = shadow.bias;
        viewConstants.cameraForward = shadow.cameraForward;
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            viewConstants.shadowProjection[cascade] = math::transpose(shadow.lightViewProjection[cascade]);
        }
        const auto lightCount = context.lights ? context.lights->size() : std::size_t{};
        const auto storedLights = (std::min)(lightCount, std::size_t{64});
        viewConstants.lightCount = static_cast<uint32_t>(storedLights);
        if (context.lights)
        {
            std::copy_n(context.lights->begin(), storedLights, viewConstants.lights);
        }
        sceneData->viewConstants = device_->UploadConstants(&viewConstants, sizeof(viewConstants));
        if (!sceneData->viewConstants.IsValid())
        {
            return Fail(error, "METEOR common scene view constants allocation failed.");
        }
        if (shadow.enabled)
        {
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                SceneShadowConstants values{};
                values.viewProjection = viewConstants.shadowProjection[cascade];
                sceneData->shadowConstants[cascade] = device_->UploadConstants(&values, sizeof(values));
                if (!sceneData->shadowConstants[cascade].IsValid())
                {
                    return Fail(error, "METEOR common shadow view constants allocation failed.");
                }
            }
        }
        nextPreparationPhase(ce::marker<"MaterialSceneCasterSnapshot">());
        if (preparedChunks >= UINT32_MAX)
        {
            return Fail(error, "METEOR caster snapshot exceeds the frame geometry address contract.");
        }
        ce::render_arena_vector<own::shared_owner<const Program>> drawPrograms(candidate->input->Draws().size(),
                                                                               &scratch.m_storage->Resource(2));
        struct DrawPreparation
        {
            own::shared_owner<const RenderBindings> bindings;
            RHIBufferSlice visibleOwner;
            std::pair<uint32_t, uint32_t> materialOffsets;
            bool shadowEnabled{};
            std::array<RHIBufferSlice, 3> shadowConstants;
        };
        ce::render_arena_vector<DrawPreparation> preparedDraws(drawPrograms.size(), &scratch.m_storage->Resource(2));
        for (const auto& draw : candidate->input->Draws())
        {
            const auto index = static_cast<std::size_t>(&draw - candidate->input->Draws().data());
            if (!PrepareProgram(context, *draw.material, drawPrograms[index], error))
            {
                return false;
            }
        }
        const bool parallelPreparation =
            preparedChunks >= 2 && !thread_pool::is_worker_thread() && scheduler_.is_running();
        // This producer owns the material cache/bytes and volume bindings. Mesh
        // preparation owns geometry caches and Draw geometry fields. Neither
        // producer reads the other's output until the explicit join below.
        // The task owner follows all captured material inputs/outputs so an early
        // return drains the producer before their destruction.
        ScenePreparationTasks drawPreparation(scheduler_, parallelPreparation);
        if (!drawPreparation.Add(
                ce::marker<"MaterialSceneDrawBindingsTask">(),
                [&](std::string& error)
                {
                    for (const auto& draw : candidate->input->Draws())
                    {
                        ce::profile_scope drawProfile{ce::marker<"MaterialSceneDrawBindingsPrepare">()};
                        std::optional<ce::profile_scope> drawPhase;
                        const auto nextDrawPhase = [&](ce::marker_id marker)
                        {
                            drawPhase.reset();
                            drawPhase.emplace(marker);
                        };
                        nextDrawPhase(ce::marker<"MaterialSceneDrawProgramBindings">());
                        const auto inputIndex = static_cast<std::size_t>(&draw - candidate->input->Draws().data());
                        const auto& program = drawPrograms[inputIndex];
                        own::shared_owner<const RenderBindings> bindings;
                        if (!bindings_.PrepareBorrowed(*device_, *context.textureCache,
                                                       candidate->input->MaterialBorrow(draw), *program->layoutSnapshot,
                                                       bindings, error, candidate->input->MaterialPins(),
                                                       context.textureFramePins ? &*context.textureFramePins : nullptr,
                                                       program->layoutSnapshot))
                        {
                            return false;
                        }
                        nextDrawPhase(ce::marker<"MaterialSceneDrawConstants">());
                        if (program->hasVolume)
                        {
                            SceneVolumeBinding volume;
                            volume.pipeline = program->volume.GetGeneration();
                            if (!bindings_.PrepareBorrowed(
                                    *device_, *context.textureCache, candidate->input->MaterialBorrow(draw),
                                    *program->volumeLayoutSnapshot, volume.material, error,
                                    candidate->input->MaterialPins(),
                                    context.textureFramePins ? &*context.textureFramePins : nullptr,
                                    program->volumeLayoutSnapshot))
                            {
                                return false;
                            }
                            const std::array<std::uint32_t, 4> constants{
                                static_cast<std::uint32_t>(candidate->effectStage.volume.bindings.size()), 0, 0, 0};
                            volume.constants = device_->UploadConstants(constants.data(), sizeof(constants));
                            if (!volume.constants.IsValid())
                            {
                                return Fail(error, "METEOR Scene Volume coefficient constants allocation failed.");
                            }
                            candidate->effectStage.volume.bindings.push_back(std::move(volume));
                        }
                        const auto owner = 0x80000001u + static_cast<std::uint32_t>(inputIndex);
                        const bool ordered =
                            program->hasSurface && (draw.queue == SceneCoverage::Blended || program->hasTransmission);
                        RHIBufferSlice visibleOwner;
                        if (ordered)
                        {
                            visibleOwner = device_->AllocateUpload({sizeof(owner), RHIUploadUsage::BufferCopy, 256});
                            if (!visibleOwner.IsWritable())
                            {
                                return Fail(error, "METEOR Scene visible owner allocation failed.");
                            }
                            std::memcpy(visibleOwner.cpuAddress, &owner, sizeof(owner));
                        }
                        std::pair<uint32_t, uint32_t> materialOffsets;
                        if (!sceneData->AppendMaterial(*bindings, materialOffsets, error))
                        {
                            return false;
                        }
                        bool shadowEnabled{};
                        std::array<RHIBufferSlice, 3> shadowConstants;
                        if (program->hasSurface && !program->editorPreview && shadow.enabled && draw.queue != SceneCoverage::Blended)
                        {
                            if (!RenderBindingCache::ValidatePass(*device_, *bindings, program->shadowLayout, error))
                            {
                                return false;
                            }
                            shadowEnabled = true;
                            shadowConstants = sceneData->shadowConstants;
                        }
                        preparedDraws[inputIndex] = {std::move(bindings), visibleOwner, materialOffsets, shadowEnabled,
                                                     shadowConstants};
                    }
                    return true;
                },
                error))
        {
            return false;
        }
        ce::render_arena_vector<std::uint32_t> shadowChunkIndices(preparedChunks, &scratch.m_storage->Resource(3));
        ce::render_arena_vector<own::shared_owner<const MeshSurfaceBatch>> shadowMeshletGeometry{
            &scratch.m_storage->Resource(3)};
        auto& casterPlan =
            m_preparationScratch->Packets(candidate->input->View().viewId, candidate->input->View().sceneEpoch)
                .m_casters;
        const bool casterReused =
            casterPlan.Prepare(*candidate->input, std::span(drawPrograms.data(), drawPrograms.size()), shadow.enabled);
        ce::profile_instant(casterReused ? ce::marker<"MaterialSceneCasterPlanReuse">()
                                         : ce::marker<"MaterialSceneCasterPlanBuild">());
        if (casterPlan.m_entries.size() != preparedChunks)
        {
            return Fail(error, "METEOR caster plan differs from the sealed chunk layout.");
        }
        for (const auto& entry : casterPlan.m_entries)
        {
            const auto inputIndex = entry.m_input;
            const auto chunkIndex = entry.m_chunk;
            const auto outputIndex = entry.m_output;
            const auto& draw = candidate->input->Draws()[inputIndex];
            const auto& chunk = draw.geometry->Chunks()[chunkIndex];
            auto& item = candidate->draws[outputIndex];
            item.program = drawPrograms[inputIndex];
            item.blended = entry.m_blended;
            item.inputIndex = inputIndex;
            item.geometryRecord = static_cast<uint32_t>(1 + outputIndex);
            item.doubleSided = entry.m_doubleSided;
            item.shadowEnabled = entry.m_shadowEnabled;
            if (!prepareGeometry(chunk.input, item.geometry))
            {
                return false;
            }
            if (draw.previousGeometry && draw.previousGeometry.operator->() != draw.geometry.operator->() &&
                chunkIndex < draw.previousGeometry->Chunks().size())
            {
                if (!prepareGeometry(draw.previousGeometry->Chunks()[chunkIndex].input, item.previousGeometry))
                {
                    return false;
                }
            }
            else
            {
                item.previousGeometry = item.geometry;
            }
            shadowChunkIndices[outputIndex] = static_cast<std::uint32_t>(chunkIndex);
            if (item.shadowEnabled)
            {
                // A separate value snapshot: the worker never reads Draw records
                // while their material bindings and scene records are being filled.
                candidate->shadowDraws.push_back(item);
            }
        }

        if (preparedChunks && !visibility_.PrepareMeshletPipelines(context, error))
        {
            return false;
        }
        // 입력 표면이 없는 뷰는 카메라 HZB를 만들지 않는다. 이때는 기존 light-space 경로를 유지한다.
        // Draw의 봉인된 geometry/program만 읽으며 뒤의 준비 작업이 채울 material binding은 보지 않는다.
        if (candidate->shadow && budget.m_shadowDepthPrediction &&
            std::ranges::any_of(candidate->draws,
                                [&](const auto& draw) { return !draw.blended && candidate->UsesMeshlets(draw); }))
        {
            candidate->shadowFirstCullStage.m_cameraPredictionView =
                GpuGeometryOcclusion::CaptureView(context, candidate->input->ViewProjection());
        }
        // Destruction drains setup before its cascade handles, then releases snapshots.
        // Setup is the sole writer of shadow caches/histories and its own Draw list.
        ScenePreparationTasks shadowPreparation(scheduler_, parallelPreparation);
        ScenePreparationTasks shadowSetupPreparation(scheduler_, parallelPreparation);
        if (!shadowSetupPreparation.Add(
                ce::marker<"MaterialSceneShadowSetupTask">(),
                [&](std::string& error)
                {
                    ce::profile_scope setupProfile{ce::marker<"MaterialSceneShadowSetup">()};
                    for (auto& draw : candidate->shadowDraws)
                    {
                        if (!draw.shadowEnabled)
                        {
                            continue;
                        }
                        if (!candidate->UsesMeshlets(draw))
                        {
                            return Fail(error, "METEOR shadow requires cooked meshlets and supported mesh indirect "
                                               "dispatch; reimport the asset.");
                        }
                        draw.shadowMeshletBin = static_cast<std::uint32_t>(shadowMeshletGeometry.size());
                        shadowMeshletGeometry.push_back(draw.geometry);
                    }
                    if (!shadowMeshletGeometry.empty())
                    {
                        if (!shadowMerge_.IsValid())
                        {
                            const RHIPipelineLayoutParam parameters[]{
                                RHILayout::SrvTable(1, 0, RHIShaderVisibility::Pixel)};
                            const auto layout = context.rootSignatures->GetOrCreate({parameters, {}}, error);
                            LX::Runtime::CompiledGraphics shader;
                            RHIShaderCompileOptions options;
                            options.strictMath = true;
                            if (!layout.IsValid() || !LX::Runtime::CompileGraphics(
                                                         RHIShaderSource::Resolve("MeteorShadowDepth.slang").string(),
                                                         "ShadowDepthVS", "ShadowDepthPS", {}, options, shader, error))
                            {
                                return false;
                            }
                            RHIGraphicsPipelineDesc desc;
                            desc.layout = layout;
                            desc.vsBytecode = shader.vertex.bytecode.Data();
                            desc.vsSize = shader.vertex.bytecode.Size();
                            desc.psBytecode = shader.pixel.bytecode.Data();
                            desc.psSize = shader.pixel.bytecode.Size();
                            desc.numRenderTargets = 0;
                            desc.dsvFormat = RHIFormat::D32Float;
                            desc.depthEnable = true;
                            desc.depthWriteMask = RHIDepthWrite::All;
                            desc.depthFunc = RHICompareOp::Less;
                            desc.cullMode = RHICullMode::None;
                            LX::Runtime::GraphicsShaderDescription description;
                            description.compile = std::move(shader.identity);
                            if (!shadowMerge_.Create(*context.psoManager, desc, std::move(description), error))
                            {
                                return false;
                            }
                        }
                        candidate->shadowRasterStage.merge = shadowMerge_.GetGeneration();
                        auto shadowContext = context;
                        ce::profile_scope shadowProfile{ce::marker<"MaterialSceneShadowPrepare">()};
                        std::optional<ce::profile_scope> shadowPhase;
                        const auto nextShadowPhase = [&](ce::marker_id marker)
                        {
                            shadowPhase.reset();
                            shadowPhase.emplace(marker);
                        };
                        nextShadowPhase(ce::marker<"MaterialSceneShadowCacheKey">());
                        shadowContext.width = shadowContext.height = candidate->shadowExtent;
                        shadowContext.displayWidth = shadowContext.displayHeight = candidate->shadowExtent;
                        const auto& view = candidate->input->View();
                        const auto viewKey = std::pair{view.sceneEpoch, view.viewId};
                        std::uint64_t completed{};
                        {
                            std::lock_guard lock(recordingMutex_);
                            completed = completed_;
                        }
                        std::erase_if(shadowCaches_,
                                      [&](const auto& entry) { return entry.first.first != view.sceneEpoch; });
                        std::erase_if(shadowHistories_,
                                      [&](const auto& entry) { return entry.first.first != view.sceneEpoch; });
                        // 현재 view/world/residency는 값으로 비교하고, 봉인된 partition은 약한 소유 신원으로 비교한다.
                        // 원본 geometry 바이트를 매 프레임 복사하지 않으며 hash-only hit도 허용하지 않는다.
                        std::vector<std::byte> key;
                        std::vector<own::weak_owner<const MeshSurfaceInput>> geometryIdentities;
                        geometryIdentities.reserve(candidate->input->Draws().size());
                        constexpr std::uint64_t maxKeyBytes = 64ull << 20;
                        std::uint64_t keyBytes{};
                        bool keyOverflow{};
                        const auto accountBytes = [&](std::uint64_t bytes)
                        {
                            if (bytes > maxKeyBytes - keyBytes)
                            {
                                keyOverflow = true;
                                keyBytes = maxKeyBytes;
                                return false;
                            }
                            keyBytes += bytes;
                            return true;
                        };
                        const auto appendBytes = [&](std::span<const std::byte> bytes)
                        {
                            if (accountBytes(bytes.size()))
                            {
                                key.insert(key.end(), bytes.begin(), bytes.end());
                            }
                        };
                        const auto append = [&](const auto& value)
                        {
                            const auto bytes = std::as_bytes(std::span{&value, std::size_t{1}});
                            appendBytes(bytes);
                        };
                        const auto appendRange = [&](const auto& range)
                        {
                            append(range.size());
                            appendBytes(std::as_bytes(std::span{range.data(), range.size()}));
                        };
                        const auto account = [&](const auto& value) { accountBytes(sizeof(value)); };
                        const auto accountRange = [&](const auto& range)
                        {
                            account(range.size());
                            accountBytes(std::as_bytes(std::span{range.data(), range.size()}).size());
                        };
                        append(view.sceneEpoch);
                        append(view.viewId);
                        append(view.historyRevision);
                        append(view.temporalFrame.historyRevision);
                        append(candidate->shadowExtent);
                        append(candidate->input->GeometryMaxPixelError());
                        for (const auto& projection : shadow.lightViewProjection)
                        {
                            append(projection);
                        }
                        for (auto& draw : candidate->shadowDraws)
                        {
                            const auto& inputDraw = candidate->input->Draws()[draw.inputIndex];
                            // Opaque rigid geometry has no material-driven depth or alpha discard.
                            // Masked and skinned casters always stay in the dynamic path.
                            draw.staticShadow =
                                inputDraw.queue == SceneCoverage::Opaque && draw.geometry->Input()->Bones().empty();
                            if (!draw.shadowEnabled || !draw.staticShadow)
                            {
                                continue;
                            }
                            append(inputDraw.model.modelId);
                            append(inputDraw.model.generation);
                            append(inputDraw.geometryKey);
                            append(inputDraw.temporalObjectId);
                            append(inputDraw.temporalIncarnation);
                            append(inputDraw.temporalInstanceId);
                            append(draw.geometryRecord);
                            append(shadowChunkIndices[draw.geometryRecord - 1]);
                            append(draw.doubleSided);
                            append(draw.geometry->ObjectToWorld());
                            append(draw.geometry->UsesDirectClusters());
                            append(bool(draw.geometry->ClusterResidency()));
                            if (const auto& resident = draw.geometry->ClusterResidency())
                            {
                                appendRange(resident->groups);
                                const auto& grid = draw.geometry->Input()->ClusterArchive()->grid;
                                appendRange(grid.origin);
                                appendRange(grid.step);
                            }
                            // root source는 generation/LOD/layout/chunk budget까지 확정된 불변 partition의 신원이다.
                            // weak token은 CPU/GPU payload를 보유하지 않고 만료/재봉인 시 기존 depth를 재사용하지 않는다.
                            if (!accountBytes(sizeof(own::weak_owner<const MeshSurfaceInput>)))
                            {
                                return Fail(error, "Shadow static-cache identity exceeds its 64 MiB bound.");
                            }
                            geometryIdentities.push_back(inputDraw.geometry->SceneTopologyIdentity());
                            // 복사하던 geometry의 논리 바이트 상한은 계속 검사한다. streamed topology의 비어 있는
                            // CPU 배열은 읽지 않으며 크기만 계산한다.
                            const auto& mesh = draw.geometry->Input()->Geometry();
                            account(mesh.vertexAttributeMask);
                            account(mesh.vertexStride);
                            account(mesh.vertexLayoutHash);
                            account(mesh.vertexBytes);
                            const bool streamedTopology = draw.geometry->Input()->StreamedTopology();
                            account(streamedTopology);
                            if (!streamedTopology)
                            {
                                accountBytes(mesh.vertexBytes);
                            }
                            account(mesh.indexCount);
                            if (!streamedTopology)
                            {
                                accountBytes(std::uint64_t(mesh.indexCount) * sizeof(std::uint32_t));
                            }
                            const auto& meshlets = draw.geometry->Input()->Meshlets();
                            account(meshlets.geometryDigest);
                            account(meshlets.settings);
                            account(meshlets.lod0);
                            accountRange(meshlets.descriptors);
                            accountRange(meshlets.vertexRemap);
                            accountRange(meshlets.triangleIndices);
                            accountRange(meshlets.primitiveRemap);
                            accountRange(draw.geometry->Input()->SourceVertexIds());
                            const auto& hierarchy = draw.geometry->Input()->Hierarchy();
                            account(hierarchy.rootGroup);
                            account(hierarchy.groupCount);
                            accountBytes(
                                std::as_bytes(std::span{hierarchy.groups.data(), hierarchy.groups.size()}).size());
                            if (keyOverflow)
                            {
                                return Fail(error, "Shadow static-cache identity exceeds its 64 MiB bound.");
                            }
                        }
                        nextShadowPhase(ce::marker<"MaterialSceneShadowCacheLookup">());
                        auto& cached = shadowCaches_[viewKey];
                        std::uint64_t newestCompletedCache{};
                        for (const auto& item : cached)
                        {
                            if (item->completion && item->completion <= completed)
                            {
                                newestCompletedCache = (std::max)(newestCompletedCache, item->frameId);
                            }
                        }
                        std::erase_if(cached,
                                      [&](const auto& item)
                                      {
                                          return item->completion && item->completion <= completed &&
                                                 item->frameId < newestCompletedCache;
                                      });
                        const bool reset = view.temporalFrame.realFrameId && view.temporalFrame.reset;
                        for (const auto& item : cached)
                        {
                            if (!reset && item->device == device_ && item->completion &&
                                item->completion <= completed && item->frameId < context.frameId && item->key == key &&
                                item->MatchesGeometry(geometryIdentities))
                            {
                                candidate->shadowCacheStage.cache = item;
                                candidate->shadowCacheStage.hit = true;
                            }
                        }
                        if (!candidate->shadowCacheStage.hit)
                        {
                            std::erase_if(shadowCacheAllocations_, [](const auto& item) { return item.expired(); });
                            const auto bytes =
                                std::uint64_t(candidate->shadowExtent) * candidate->shadowExtent * 4u * 3u;
                            std::uint64_t resident = bytes;
                            for (const auto& item : shadowCacheAllocations_)
                            {
                                if (const auto owner = item.lock())
                                {
                                    resident += owner->bytes;
                                }
                            }
                            if (!bytes || resident > budget.shadowCacheBytes)
                            {
                                return Fail(
                                    error,
                                    "Shadow static-cache allocations exceed the retained/in-flight byte budget.");
                            }
                            auto cache = own::make_shared<ShadowCache>();
                            cache->device = device_;
                            cache->extent = candidate->shadowExtent;
                            cache->bytes = bytes;
                            cache->frameId = context.frameId;
                            cache->key = std::move(key);
                            cache->geometryIdentities = std::move(geometryIdentities);
                            for (auto& depth : cache->depths)
                            {
                                RHITextureDesc desc;
                                desc.width = desc.height = cache->extent;
                                desc.format = RHIFormat::D32Float;
                                desc.allowDepthStencil = true;
                                desc.debugName = L"METEOR.Shadow.StaticCache";
                                if (!device_->CreateTexture(desc, depth, error))
                                {
                                    return false;
                                }
                            }
                            shadowCacheAllocations_.push_back(cache);
                            candidate->shadowCacheStage.cache = std::move(cache);
                        }
                        auto& histories = shadowHistories_[viewKey];
                        std::uint64_t newestCompletedHistory{};
                        for (const auto& item : histories)
                        {
                            if (item.cascades[0] && item.cascades[0]->completion &&
                                item.cascades[0]->completion <= completed)
                            {
                                newestCompletedHistory = (std::max)(newestCompletedHistory, item.frameId);
                            }
                        }
                        std::erase_if(histories,
                                      [&](const auto& item)
                                      {
                                          return item.cascades[0] && item.cascades[0]->completion &&
                                                 item.cascades[0]->completion <= completed &&
                                                 item.frameId < newestCompletedHistory;
                                      });
                        nextShadowPhase(ce::marker<"MaterialSceneShadowCascades">());
                        for (unsigned cascade = 0; cascade < 3; ++cascade)
                        {
                            ce::profile_scope cascadeProfile{ce::marker<"MaterialSceneShadowCascadePrepare">()};
                            own::shared_owner<const GpuGeometryOcclusion::Pyramid> previous;
                            for (const auto& history : histories)
                            {
                                const auto& old = history.cascades[cascade];
                                if (!candidate->shadowFirstCullStage.m_cameraPredictionView && !reset &&
                                    history.frameId == newestCompletedHistory && history.frameId < context.frameId &&
                                    history.revision == view.historyRevision &&
                                    history.temporalRevision == view.temporalFrame.historyRevision && old &&
                                    old->completion && old->completion <= completed &&
                                    old->source.width == candidate->shadowExtent &&
                                    old->source.height == candidate->shadowExtent)
                                {
                                    previous = GpuGeometryOcclusion::ReadHistory(
                                        shadowContext, shadow.lightViewProjection[cascade], old);
                                }
                            }
                            const auto pixelError =
                                shadow.mapResolution ? candidate->input->GeometryMaxPixelError() : 0.f;
                            if (!shadowOcclusion_.Prepare(shadowContext, shadow.lightViewProjection[cascade],
                                                          candidate->shadowFinalCullStage.pyramids[cascade], error,
                                                          "METEOR.Shadow.Occlusion.C" + std::to_string(cascade)) ||
                                !shadowOcclusion_.PrepareHistory(shadowContext, shadow.lightViewProjection[cascade],
                                                                 view.viewId, view.historyRevision,
                                                                 candidate->shadowHistoryStage.next[cascade], error,
                                                                 "METEOR.Shadow.History.C" + std::to_string(cascade)))
                            {
                                return false;
                            }
                            if (!shadowPreparation.Add(
                                    ce::marker<"MaterialSceneShadowVisibilityTask">(),
                                    [&, shadowContext, previous, pixelError, cascade](std::string& taskError)
                                    {
                                        const auto& prediction = candidate->shadowFirstCullStage.m_cameraPredictionView;
                                        if (!visibility_.PrepareMeshlets(
                                                shadowContext, shadow.lightViewProjection[cascade],
                                                shadowMeshletGeometry,
                                                candidate->shadowFirstCullStage.meshlets[cascade], taskError,
                                                bool(previous) || bool(prediction), previous, {}, false, pixelError,
                                                candidate->shadowExtent, candidate->shadowExtent,
                                                prediction ? &*prediction : nullptr) ||
                                            !visibility_.PrepareMeshlets(
                                                shadowContext, shadow.lightViewProjection[cascade],
                                                shadowMeshletGeometry,
                                                candidate->shadowFinalCullStage.meshlets[cascade], taskError, true, {},
                                                candidate->shadowFirstCullStage.meshlets[cascade], false, pixelError,
                                                candidate->shadowExtent, candidate->shadowExtent))
                                        {
                                            return false;
                                        }
                                        if (!candidate->shadowCacheStage.hit &&
                                            !visibility_.PrepareMeshlets(
                                                shadowContext, shadow.lightViewProjection[cascade],
                                                shadowMeshletGeometry, candidate->shadowCacheStage.meshlets[cascade],
                                                taskError, false, {}, {}, false, pixelError, candidate->shadowExtent,
                                                candidate->shadowExtent))
                                        {
                                            return false;
                                        }
                                        return true;
                                    },
                                    error))
                            {
                                return false;
                            }
                        }
                    }
                    return true;
                },
                error))
        {
            return false;
        }
        // 기하 소유자 목록은 여기서 봉인된다. 가시성 작업은 뒤에서 채울 Draw/장면 레코드를 읽지 않는다.
        // 카메라 목록 작업 소유자는 빌린 입력보다 뒤에 두어 Draw 업로드 실패에도 먼저 배출한다.
        nextPreparationPhase(ce::marker<"MaterialSceneCameraPrepare">());
        auto& cameraGeometry = scratch.m_cameraGeometry;
        auto& blendedGeometry = scratch.m_blendedGeometry;
        cameraGeometry.clear();
        blendedGeometry.clear();
        cameraGeometry.reserve(preparedChunks);
        blendedGeometry.reserve(preparedChunks);
        for (auto& draw : candidate->draws)
        {
            if (candidate->UsesMeshlets(draw))
            {
                auto& list = draw.blended ? blendedGeometry : cameraGeometry;
                draw.cameraMeshletBin = static_cast<uint32_t>(list.size());
                list.push_back(draw.geometry);
            }
        }
        own::shared_owner<const GpuGeometryOcclusion::Pyramid> previousCameraHistory;
        if (!cameraGeometry.empty())
        {
            const auto& view = candidate->input->View();
            auto historyCamera = view.camera;
            if (view.temporalFrame.camera.valid)
            {
                std::memcpy(&historyCamera.projection, view.temporalFrame.camera.projectionMatrix.data(),
                            sizeof(historyCamera.projection));
            }
            std::uint64_t completed;
            {
                std::lock_guard lock(recordingMutex_);
                completed = completed_;
            }
            // Only completed, accepted history can enter another recording.
            // Keep the newest completed view plus its still-submitted successors.
            std::erase_if(cameraHistories_, [&](const auto& entry) { return entry.first.first != view.sceneEpoch; });
            auto& histories = cameraHistories_[{view.sceneEpoch, view.viewId}];
            std::uint64_t newest{};
            for (const auto& item : histories)
            {
                if (item.pyramid->completion <= completed)
                {
                    newest = (std::max)(newest, item.pyramid->source.frameId);
                }
            }
            std::erase_if(histories, [&](const auto& item)
                          { return item.pyramid->completion <= completed && item.pyramid->source.frameId < newest; });
            for (const auto& item : histories)
            {
                const auto& h = *item.pyramid;
                if (h.source.frameId == newest && h.source.frameId < view.frameId && h.completion <= completed &&
                    h.source.width == view.width && h.source.height == view.height &&
                    h.revision == view.historyRevision && historyCamera.HasSameTemporalHistory(item.camera) &&
                    item.temporalRevision == view.temporalFrame.historyRevision &&
                    (!view.temporalFrame.realFrameId || !view.temporalFrame.reset))
                {
                    previousCameraHistory =
                        GpuGeometryOcclusion::ReadHistory(context, candidate->input->ViewProjection(), item.pyramid);
                }
            }
            if (!cameraOcclusion_.PrepareHistory(context, candidate->input->ViewProjection(), view.viewId,
                                                 view.historyRevision, candidate->historyStage.nextCameraHistory,
                                                 error))
            {
                return false;
            }
            // The same builder produces the current pyramid from the occluder depth.
            if (!cameraOcclusion_.Prepare(context, candidate->input->ViewProjection(),
                                          candidate->finalCullStage.currentPyramid, error))
            {
                return false;
            }
        }
        nextPreparationPhase(ce::marker<"MaterialSceneCameraDispatch">());
        ScenePreparationTasks cameraPreparation(scheduler_, parallelPreparation);
        if (!cameraGeometry.empty())
        {
            if (!cameraPreparation.Add(
                    ce::marker<"MaterialSceneCameraVisibilityTask">(),
                    [&, previousCameraHistory](std::string& taskError)
                    {
                        if (!visibility_.PrepareMeshlets(context, candidate->input->ViewProjection(), cameraGeometry,
                                                         candidate->firstCullStage.cameraDepthMeshlets, taskError,
                                                         false, previousCameraHistory, {}, false,
                                                         candidate->input->GeometryMaxPixelError()) ||
                            !visibility_.PrepareMeshlets(context, candidate->input->ViewProjection(), cameraGeometry,
                                                         candidate->finalCullStage.cameraMeshlets, taskError, true, {},
                                                         candidate->firstCullStage.cameraDepthMeshlets, false,
                                                         candidate->input->GeometryMaxPixelError()))
                        {
                            return false;
                        }
                        return true;
                    },
                    error))
            {
                return false;
            }
        }
        // Blended surfaces were never depth-occlusion culled: frustum only, and
        // survivors keep source meshlet order so blending stays deterministic.
        if (!blendedGeometry.empty())
        {
            if (!cameraPreparation.Add(
                    ce::marker<"MaterialSceneBlendedVisibilityTask">(),
                    [&](std::string& taskError)
                    {
                        return visibility_.PrepareMeshlets(context, candidate->input->ViewProjection(), blendedGeometry,
                                                           candidate->finalCullStage.blendedMeshlets, taskError, false,
                                                           {}, {}, true, candidate->input->GeometryMaxPixelError());
                    },
                    error))
            {
                return false;
            }
        }
        {
            ce::profile_scope joinProfile{ce::marker<"MaterialSceneDrawBindingsJoin">()};
            if (!drawPreparation.Join(error))
            {
                return false;
            }
        }
        nextPreparationPhase(ce::marker<"MaterialSceneDrawsPrepare">());
        SceneConstants constants = viewConstants;
        for (const auto& draw : candidate->input->Draws())
        {
            ce::profile_scope drawProfile{ce::marker<"MaterialSceneDrawPrepare">()};
            std::optional<ce::profile_scope> drawPhase;
            const auto nextDrawPhase = [&](ce::marker_id marker)
            {
                drawPhase.reset();
                drawPhase.emplace(marker);
            };
            const auto inputIndex = static_cast<std::size_t>(&draw - candidate->input->Draws().data());
            const auto& program = drawPrograms[inputIndex];
            const auto& prepared = preparedDraws[inputIndex];
            const auto& bindings = prepared.bindings;
            constants.owner = 0x80000001u + static_cast<std::uint32_t>(inputIndex);
            constants.temporalHistoryValid = draw.temporalHistoryValid && !temporal.reset;
            constants.coverage = draw.coverage.flags;
            const bool ordered =
                program->hasSurface && (draw.queue == SceneCoverage::Blended || program->hasTransmission);
            constants.padding[0] = ordered ? 1u : 0u;
            constants.padding[2] = ordered ? (draw.queue == SceneCoverage::Blended ? 1u : 2u) : 0u;
            constants.cutoff = draw.coverage.cutoff;
            constants.lightCount = static_cast<std::uint32_t>(ordered ? lightCount : storedLights);
            const auto visibleOwner = prepared.visibleOwner;
            const auto materialOffsets = prepared.materialOffsets;
            const auto shadowEnabled = prepared.shadowEnabled;
            const auto& shadowConstants = prepared.shadowConstants;
            nextDrawPhase(ce::marker<"MaterialSceneDrawChunks">());
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
                const auto chunkIndex = static_cast<std::size_t>(&chunk - draw.geometry->Chunks().data());
                const auto outputIndex = candidate->inputRanges[inputIndex].first + chunkIndex;
                item.geometry = candidate->draws[outputIndex].geometry;
                item.previousGeometry = candidate->draws[outputIndex].previousGeometry;
                item.cameraMeshletBin = candidate->draws[outputIndex].cameraMeshletBin;
                const auto& cpuChunk = scratch.m_chunks[outputIndex];
                constants.geometryIdentity = cpuChunk.identity;
                if (geometryTable->records.size() >= UINT32_MAX || item.geometry->MeshletCount() > (UINT32_MAX >> 7))
                {
                    return Fail(error, "METEOR visibility address exceeds its frame geometry contract.");
                }
                ce::profile_scope chunkRecordProfile{ce::marker<"MaterialSceneChunkRecordUpload">()};
                SceneFrameGeometry::Record record;
                record.identity = constants.geometryIdentity;
                record.chunkIndex = static_cast<uint32_t>(chunkIndex);
                record.owner = constants.owner;
                record.meshletCount = item.geometry->MeshletCount();
                record.vertexCount = static_cast<uint32_t>(chunk.input->Count());
                std::copy(cpuChunk.worldColumns.begin(), cpuChunk.worldColumns.end(), record.worldColumns);
                constants.world = cpuChunk.world;
                constants.previousWorld = cpuChunk.previousWorld;
                constants.geometryIdentity.reserved[0] = static_cast<uint32_t>(1 + outputIndex);
                item.geometryRecord = constants.geometryIdentity.reserved[0];
                if (!SceneVisibilityContract::Reserve(record.meshletCount, geometryTable->nextPrimitiveAddress,
                                                      constants.geometryIdentity.reserved[1],
                                                      constants.geometryIdentity.reserved[2]))
                {
                    return Fail(error,
                                "METEOR frame primitive address capacity exceeded; no truncated visibility IDs.");
                }
                record.identity.reserved[1] = constants.geometryIdentity.reserved[1];
                record.identity.reserved[2] = constants.geometryIdentity.reserved[2];
                constants.padding[0] = ordered ? 1u : 0u;
                item.constants =
                    ordered ? device_->UploadConstants(&constants, sizeof(constants)) : sceneData->viewConstants;
                if (ordered)
                {
                    constants.padding[0] = 2;
                    item.referenceConstants = device_->UploadConstants(&constants, sizeof(constants));
                }
                if (!item.constants.IsValid() || (ordered && !item.referenceConstants.IsValid()))
                {
                    return Fail(error, "METEOR per-chunk geometry constants allocation failed.");
                }
                record.identity.reserved[0] = 0;
                geometryTable->records[1 + outputIndex] = record;
                geometryTable->geometry[1 + outputIndex] = item.geometry;
                auto& gpuRecord = sceneData->records[item.geometryRecord];
                gpuRecord.identity = constants.geometryIdentity;
                gpuRecord.worldColumns = cpuChunk.worldColumns;
                gpuRecord.previousWorldColumns = cpuChunk.previousWorldColumns;
                gpuRecord.material = {materialOffsets.first, materialOffsets.second, constants.owner,
                                      constants.coverage};
                gpuRecord.surface = {std::bit_cast<uint32_t>(constants.cutoff), item.geometryRecord,
                                     static_cast<uint32_t>(chunkIndex), record.meshletCount};
                gpuRecord.history = {constants.temporalHistoryValid,
                                     item.geometry.operator->() != item.previousGeometry.operator->() ? 1u : 0u,
                                     UINT32_MAX, 0u};
                // CPU seals geometry/material command ranges; compute selects
                // cooked replacement groups and compacts meshlets. Ordered lists
                // preserve the source primitive and layer order.
                // The compiler accepts Surface and/or Volume closures; emission
                // belongs to Surface, not a separate color-only raster program.
                // Surface color/lookup passes require hasSurface. Special
                // capture entry points key on their feature bits instead, so
                // cover those entries too. Volume-only programs emit no mesh
                // raster work and keep their existing compute/composite route.
                const bool rasterGeometry = program->hasSurface || program->hasSpecial;
                if (rasterGeometry &&
                    !sceneData->AssignGeometry(item.geometryRecord, item.geometry, item.previousGeometry, error))
                {
                    return false;
                }
                // Opaque and blended surfaces are culled per meshlet; the owner gate is
                // the sealed visibleOwner. No instance-level culling route exists.
                if (rasterGeometry && !candidate->UsesMeshlets(item))
                {
                    return Fail(error,
                                ordered
                                    ? "METEOR blended surface requires supported cooked meshlets; reimport the asset."
                                    : "METEOR opaque surface requires supported cooked meshlets; reimport the asset.");
                }

                candidate->draws[outputIndex] = std::move(item);
            }
        }
        nextPreparationPhase(ce::marker<"MaterialSceneGeometryPublish">());
        if (!candidate->geometryStage.Prepare(
                std::move(geometryTable), error, &sceneResidentBuffers_,
                {candidate->input->View().sceneEpoch, candidate->input->View().viewId, 0}))
        {
            return false;
        }
        if (!cameraGeometry.empty() && !hybridPipelines_.Prepare(context, error))
        {
            return false;
        }
        nextPreparationPhase(ce::marker<"MaterialSceneCameraJoin">());
        if (!cameraPreparation.Join(error))
        {
            return false;
        }
        // 표면 단계는 완성된 Draw/기하 테이블과 첫 목록·복구 목록을 함께 소비한다.
        nextPreparationPhase(ce::marker<"MaterialSceneSurfaceStagesPrepare">());
        if (!candidate->occluderStage.Prepare(*candidate, context, candidate->geometryStage.geometryTable,
                                              candidate->firstCullStage.cameraDepthMeshlets, hybridPipelines_, error) ||
            !candidate->visibilityStage.Prepare(*candidate, context, candidate->geometryStage.geometryTable,
                                                candidate->finalCullStage.cameraMeshlets, hybridPipelines_, error,
                                                candidate->occluderStage.occluderVisibility.operator->()))
        {
            return false;
        }
        {
            ce::profile_scope setupJoinProfile{ce::marker<"MaterialSceneShadowSetupJoin">()};
            if (!shadowSetupPreparation.Join(error))
            {
                return false;
            }
            // Bindings are not part of the caster snapshot. Connect the completed
            // material Draw values only after the shadow planner stops reading it.
            for (auto& draw : candidate->shadowDraws)
            {
                const auto bin = draw.shadowMeshletBin;
                const auto staticShadow = draw.staticShadow;
                draw = candidate->draws[draw.geometryRecord - 1];
                draw.shadowMeshletBin = bin;
                draw.staticShadow = staticShadow;
            }
        }
        nextPreparationPhase(ce::marker<"MaterialSceneIndirectCommandsPrepare">());
        if (!cameraGeometry.empty() || !shadowMeshletGeometry.empty() ||
            std::ranges::any_of(candidate->draws, [](const auto& draw) { return draw.program->hasSurface; }))
        {
            ce::profile_scope indirectProfile{ce::marker<"MaterialSceneIndirectPrepare">()};
            if (!indirectCommands_.GetGeneration())
            {
                const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
                                                      RHILayout::Srv(2), RHILayout::UavBufferTable(1, 0)};
                const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
                LX::Runtime::CompiledCompute compiled;
                RHIShaderCompileOptions options;
                options.strictMath = true;
                if (!layout.IsValid() ||
                    !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphIndirect.slang").string(),
                                                 "LXBuildIndirectCommands", {}, options, compiled, error))
                {
                    return false;
                }
                RHIComputePipelineDesc desc;
                desc.layout = layout;
                desc.csBytecode = compiled.stage.bytecode.Data();
                desc.csSize = compiled.stage.bytecode.Size();
                if (!indirectCommands_.Create(*context.psoManager, desc, std::move(compiled.description), error))
                {
                    return false;
                }
            }
            // GBuffer bytecode can be identical while the color/lookup shader
            // differs. A bucket must share the complete cooked program identity.
            // The exact cooked program, raster state and descriptor page form a
            // bucket. A hash, material pointer or equal GBuffer bytecode alone is
            // insufficient to merge programs with different pass semantics.
            using PacketGroups = const ScenePacketGroups*;
            auto& packetCache =
                m_preparationScratch->Packets(candidate->input->View().viewId, candidate->input->View().sceneEpoch);
            const bool reusePackets = ScenePacketReuseEnabled();
            const auto prepareGroups = [&](unsigned slot, const auto& draws, auto eligible) -> PacketGroups
            {
                ce::profile_scope groupProfile{ce::marker<"MaterialScenePacketGroupsPrepare">()};
                auto& inputs = packetCache.m_inputs;
                inputs.clear();
                for (size_t index = 0; index < draws.size(); ++index)
                {
                    const auto& draw = draws[index];
                    if (eligible(draw))
                    {
                        inputs.push_back({draw.program->generation->cooked.product.program.semanticKey,
                                          draw.doubleSided, sceneData->recordPages[draw.geometryRecord], index,
                                          draw.geometryRecord, draw.geometry->MeshletCount()});
                    }
                }
                auto& plan = packetCache.m_groups[slot];
                const bool reused = plan.Update(inputs, reusePackets);
                ce::profile_instant(reused ? ce::marker<"MaterialScenePacketGroups.Reuse">()
                                           : ce::marker<"MaterialScenePacketGroups.Rebuild">());
                inputs.clear();
                return &plan;
            };
            const auto cameraGroups = prepareGroups(0, candidate->draws, [&](const auto& draw)
                                                    { return candidate->UsesMeshlets(draw) && !draw.blended; });
            const auto groups = cameraGroups->Groups();
            const auto preparePacketPipelines = [&]()
            {
                constexpr const char* entries[]{"LXResetPacketBuckets",   "LXCountPacketBuckets",
                                                "LXScanPacketRanges",     "LXAddPacketRangeOffsets",
                                                "LXBuildPacketArguments", "LXScatterPackets"};
                for (unsigned stage = 0; stage < packetPipelines_.size(); ++stage)
                {
                    if (packetPipelines_[stage].GetGeneration())
                    {
                        continue;
                    }
                    const RHIPipelineLayoutParam params[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1),
                                                          RHILayout::Srv(2), RHILayout::UavBufferTable(4, 0)};
                    const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
                    LX::Runtime::CompiledCompute compiled;
                    RHIShaderCompileOptions options;
                    options.strictMath = true;
                    if (!layout.IsValid() ||
                        !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphIndirect.slang").string(),
                                                     entries[stage], {}, options, compiled, error))
                    {
                        return false;
                    }
                    RHIComputePipelineDesc desc;
                    desc.layout = layout;
                    desc.csBytecode = compiled.stage.bytecode.Data();
                    desc.csSize = compiled.stage.bytecode.Size();
                    if (!packetPipelines_[stage].Create(*context.psoManager, desc, std::move(compiled.description),
                                                        error))
                    {
                        return false;
                    }
                }
                return true;
            };
            if ((!groups.empty() || !shadowMeshletGeometry.empty()) && !preparePacketPipelines())
            {
                return false;
            }
            const auto prepare = [&](own::shared_owner<IndirectMaterialBatch> batch, const PacketGroups& packetGroups,
                                     ScenePacketRecords& recordCache, bool depth,
                                     own::shared_owner<const IndirectMaterialBatch>& result, std::string& taskError,
                                     std::optional<unsigned> cascade = {},
                                     own::shared_owner<const GpuGeometryVisibility::Frame> shadowList = {},
                                     const char* shadowPhase = "Final")
            {
                ce::profile_scope packetProfile{ce::marker<"MaterialScenePacketPrepare">()};
                static constexpr std::array<std::array<std::string_view, 3>, 3> shadowNames{
                    {{"METEOR.Scene.Commands.Shadow.Static.0", "METEOR.Scene.Commands.Shadow.Static.1",
                      "METEOR.Scene.Commands.Shadow.Static.2"},
                     {"METEOR.Scene.Commands.Shadow.First.0", "METEOR.Scene.Commands.Shadow.First.1",
                      "METEOR.Scene.Commands.Shadow.First.2"},
                     {"METEOR.Scene.Commands.Shadow.Final.0", "METEOR.Scene.Commands.Shadow.Final.1",
                      "METEOR.Scene.Commands.Shadow.Final.2"}}};
                const auto label = cascade ? shadowNames[std::string_view(shadowPhase) == "Static"  ? 0
                                                         : std::string_view(shadowPhase) == "First" ? 1
                                                                                                    : 2][*cascade]
                                   : depth ? std::string_view("METEOR.Scene.Commands.Depth")
                                           : std::string_view("METEOR.Scene.Commands.Surface");
                if (batch->name != label)
                {
                    batch->name.assign(label);
                }
                batch->device = device_;
                batch->pool = bufferPool_;
                for (unsigned stage = 0; stage < packetPipelines_.size(); ++stage)
                {
                    batch->packetPipelines[stage] = packetPipelines_[stage].GetGeneration();
                }
                auto& draws = cascade ? candidate->shadowDraws : candidate->draws;
                batch->visibility = cascade ? shadowList
                                    : depth ? candidate->firstCullStage.cameraDepthMeshlets
                                            : candidate->finalCullStage.cameraMeshlets;
                const bool reused = recordCache.Prepare(*packetGroups);
                ce::profile_instant(reused ? ce::marker<"MaterialScenePacketRecords.Reuse">()
                                           : ce::marker<"MaterialScenePacketRecords.Rebuild">());
                auto& records = recordCache.m_records;
                const auto selectedGroups = packetGroups->Spans();
                batch->ReserveBuckets(selectedGroups.size());
                size_t output{};
                for (const auto& indices : selectedGroups)
                {
                    const auto bucketId = static_cast<uint32_t>(batch->buckets.size());
                    auto& bucket = batch->AppendBucket();
                    bucket.representative = indices.front();
                    bucket.count = static_cast<uint32_t>(indices.size());
                    for (const auto index : indices)
                    {
                        auto& draw = draws[index];
                        if (!cascade && !depth)
                        {
                            draw.materialBucket = bucketId;
                            draw.bucketRepresentative = index == indices.front();
                        }
                        const auto bin = cascade ? draw.shadowMeshletBin : draw.cameraMeshletBin;
                        records[output++].visibleOffset = batch->visibility->MeshletOutputOffset(bin);
                    }
                }
                if (!batch->UploadPackets(records, taskError, packetGroups))
                {
                    return false;
                }
                result = std::move(batch);
                return true;
            };
            struct PacketTask
            {
                PacketGroups m_groups;
                bool m_depth{};
                own::shared_owner<const IndirectMaterialBatch>* m_output{};
                std::optional<unsigned> m_cascade;
                own::shared_owner<const GpuGeometryVisibility::Frame> m_visibility;
                const char* m_phase{"Final"};
                std::size_t m_storageIndex{};
                std::string m_error;
                std::exception_ptr m_failure;
                bool m_succeeded{};
                ScenePacketRecords* m_records{};
                own::shared_owner<IndirectMaterialBatch> m_batch;
            };
            std::array<PacketTask, 2> cameraTasks;
            std::array<PacketTask, 9> shadowTasks;
            std::size_t cameraTaskCount{}, shadowTaskCount{};
            std::atomic<std::size_t> nextCameraTask{}, nextShadowTask{};
            const auto packetErrors = [&](std::span<const PacketTask> tasks)
            {
                for (const auto& task : tasks)
                {
                    if (task.m_failure)
                    {
                        try
                        {
                            std::rethrow_exception(task.m_failure);
                        }
                        catch (const std::exception& failure)
                        {
                            error = failure.what();
                        }
                        catch (...)
                        {
                            if (!parallelPreparation)
                            {
                                throw;
                            }
                            error = "Unknown scene preparation task failure.";
                        }
                        return false;
                    }
                    if (!task.m_succeeded)
                    {
                        error = task.m_error.empty() ? "Scene preparation task failed." : task.m_error;
                        return false;
                    }
                }
                return true;
            };
            // guard는 descriptor, error, claim cursor보다 늦게 선언해야 조기 반환도 모두 drain한다.
            ScenePreparationTasks packetPreparation(scheduler_, parallelPreparation);
            const auto submitPacketTasks = [&](std::span<PacketTask> tasks, std::atomic<std::size_t>& next)
            {
                for (auto& task : tasks)
                {
                    task.m_records = &packetCache.m_records[task.m_storageIndex];
                    task.m_batch = m_preparationScratch->m_batches.Acquire();
                }
                const auto workers = parallelPreparation
                                         ? std::min(tasks.size(), std::max(std::size_t{1}, scheduler_.worker_count()))
                                         : std::size_t{1};
                const auto admitted = packetPreparation.AddIndexed(
                    ce::marker<"MaterialScenePacketTask">(), workers,
                    [&, tasks, next = &next](std::size_t, std::string&)
                    {
                        for (;;)
                        {
                            const auto index = next->fetch_add(1, std::memory_order_relaxed);
                            if (index >= tasks.size())
                            {
                                break;
                            }
                            auto& task = tasks[index];
                            try
                            {
                                task.m_succeeded =
                                    prepare(task.m_batch, task.m_groups, *task.m_records, task.m_depth, *task.m_output,
                                            task.m_error, task.m_cascade, task.m_visibility, task.m_phase);
                            }
                            catch (...)
                            {
                                task.m_failure = std::current_exception();
                            }
                            if (!parallelPreparation && (!task.m_succeeded || task.m_failure))
                            {
                                break;
                            }
                        }
                        return true;
                    },
                    error);
                // inline 실패는 다음 owner 단계 전에 보고하고 병렬 작업만 마지막 합류까지 모은다.
                return admitted && (parallelPreparation || packetErrors(tasks));
            };
            if (!groups.empty())
            {
                cameraTasks[cameraTaskCount++] = {
                    cameraGroups, false, &candidate->visibilityStage.materialCommands, {}, {}, "Final", 0};
                cameraTasks[cameraTaskCount++] = {cameraGroups, true, &candidate->occluderStage.depthCommands, {}, {},
                                                  "Final",      1};
                if (!submitPacketTasks(std::span(cameraTasks).first(cameraTaskCount), nextCameraTask))
                {
                    return false;
                }
            }
            const auto needsMaterialTiles = [&](const auto& group)
            {
                const auto& draw = candidate->draws[group.second.front()];
                return draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended;
            };
            if (std::ranges::any_of(groups, needsMaterialTiles))
            {
                ce::profile_scope materialProfile{ce::marker<"MaterialSceneTilePacketPrepare">()};
                constexpr const char* entries[]{"ResetMaterialWork",   "CountMaterialTiles",
                                                "ScanMaterialRanges",  "AddMaterialRangeOffsets",
                                                "BuildMaterialRanges", "ScatterMaterialTiles"};
                auto work = own::make_shared<SceneMaterialWork>();
                work->device = device_;
                work->pool = bufferPool_;
                work->width = context.width;
                work->height = context.height;
                work->recordCount = static_cast<uint32_t>(candidate->geometryStage.geometryTable->records.size());
                work->geometry = candidate->geometryStage.geometryTable;
                for (unsigned stage = 0; stage < materialWorkPipelines_.size(); ++stage)
                {
                    if (!materialWorkPipelines_[stage].GetGeneration())
                    {
                        const RHIPipelineLayoutParam params[]{
                            RHILayout::Cbv(0), RHILayout::SrvTable(1, 0),       RHILayout::Srv(1),
                            RHILayout::Srv(2), RHILayout::UavBufferTable(4, 0), RHILayout::Srv(3)};
                        const auto layout = context.rootSignatures->GetOrCreate({params, {}}, error);
                        LX::Runtime::CompiledCompute compiled;
                        RHIShaderCompileOptions options;
                        options.strictMath = true;
                        if (!layout.IsValid() ||
                            !LX::Runtime::CompileCompute(RHIShaderSource::Resolve("MaterialGraphWork.slang").string(),
                                                         entries[stage], {}, options, compiled, error))
                        {
                            return false;
                        }
                        RHIComputePipelineDesc desc;
                        desc.layout = layout;
                        desc.csBytecode = compiled.stage.bytecode.Data();
                        desc.csSize = compiled.stage.bytecode.Size();
                        if (!materialWorkPipelines_[stage].Create(*context.psoManager, desc,
                                                                  std::move(compiled.description), error))
                        {
                            return false;
                        }
                    }
                    work->pipelines[stage] = materialWorkPipelines_[stage].GetGeneration();
                }
                work->buckets.reserve(groups.size());
                for (const auto& group : groups)
                {
                    if (!needsMaterialTiles(group))
                    {
                        continue;
                    }
                    const auto& indices = group.second;
                    SceneMaterialWork::Bucket bucket;
                    bucket.representative = indices.front();
                    bucket.count = static_cast<uint32_t>(indices.size());
                    for (const auto index : indices)
                    {
                        auto& draw = candidate->draws[index];
                        draw.materialTileBucket = static_cast<uint32_t>(work->buckets.size());
                        sceneData->records[draw.geometryRecord].history[2] = draw.materialTileBucket;
                    }
                    work->buckets.push_back(std::move(bucket));
                }
                if (!sceneData->Upload(sceneResidentBuffers_, candidate->input->View().sceneEpoch,
                                       candidate->input->View().viewId, error))
                {
                    return false;
                }
                work->sceneRecordsOwner = sceneData->recordBuffer;
                if (!work->UploadPackets(sceneData->recordBuffer->Slice(), static_cast<uint32_t>(work->buckets.size()),
                                         error))
                {
                    return false;
                }
                candidate->materialWorkStage.materialWork = std::move(work);
            }
            {
                ce::profile_scope shadowJoinProfile{ce::marker<"MaterialSceneShadowJoin">()};
                if (!shadowPreparation.Join(error))
                {
                    return false;
                }
                for (auto& draw : candidate->shadowDraws)
                {
                    if (draw.shadowMeshletBin != UINT32_MAX)
                    {
                        for (unsigned cascade = 0; cascade < 3; ++cascade)
                        {
                            draw.shadowMeshlets[cascade] = candidate->shadowFinalCullStage.meshlets[cascade];
                        }
                    }
                }
            }
            for (const bool staticCasters : {true, false})
            {
                const auto shadowGroups =
                    prepareGroups(staticCasters ? 1u : 2u, candidate->shadowDraws, [&](const auto& draw)
                                  { return draw.shadowEnabled && draw.staticShadow == staticCasters; });
                if (shadowGroups->Groups().empty())
                {
                    continue;
                }
                for (unsigned cascade = 0; cascade < 3; ++cascade)
                {
                    if (staticCasters)
                    {
                        if (!candidate->shadowCacheStage.hit)
                        {
                            shadowTasks[shadowTaskCount++] = {shadowGroups,
                                                              false,
                                                              &candidate->shadowCacheStage.commands[cascade],
                                                              cascade,
                                                              candidate->shadowCacheStage.meshlets[cascade],
                                                              "Static",
                                                              2 + cascade};
                        }
                    }
                    else
                    {
                        shadowTasks[shadowTaskCount++] = {shadowGroups,
                                                          false,
                                                          &candidate->shadowOccluderStage.commands[cascade],
                                                          cascade,
                                                          candidate->shadowFirstCullStage.meshlets[cascade],
                                                          "First",
                                                          5 + cascade * 2};
                        shadowTasks[shadowTaskCount++] = {shadowGroups,
                                                          false,
                                                          &candidate->shadowRasterStage.shadowCommands[cascade],
                                                          cascade,
                                                          candidate->shadowFinalCullStage.meshlets[cascade],
                                                          "Final",
                                                          6 + cascade * 2};
                    }
                }
            }
            if (shadowTaskCount && !submitPacketTasks(std::span(shadowTasks).first(shadowTaskCount), nextShadowTask))
            {
                return false;
            }
            // Only transmitting/blended surfaces replay geometry. Opaque Temporal
            // consumes final visibility and the existing GPU material tile packets.
            auto replay = m_preparationScratch->m_batches.Acquire();
            replay->name = "METEOR.Scene.Commands.Temporal.Layers";
            replay->visibility = candidate->finalCullStage.blendedMeshlets;
            replay->device = device_;
            replay->pool = bufferPool_;
            replay->pipeline = indirectCommands_.GetGeneration();
            ce::render_arena_vector<uint32_t> replayWords{&scratch.m_storage->Resource(2)};
            ce::render_arena_vector<std::array<uint32_t, 4>> replayRecords{&scratch.m_storage->Resource(2)};
            const auto replayCount = static_cast<std::size_t>(std::ranges::count_if(
                candidate->draws, [](const auto& draw) { return draw.program->hasSurface && draw.blended; }));
            if (!ReserveSceneIndirectCommands(replayCount, replayWords, replayRecords, error))
            {
                return false;
            }
            RHIPipelineHandle previousPipeline{};
            uint32_t replayBytes{};
            for (std::size_t index = 0; index < candidate->draws.size(); ++index)
            {
                const auto& draw = candidate->draws[index];
                if (!draw.program->hasSurface || !draw.blended)
                {
                    continue;
                }
                const auto pipeline = draw.program->temporal[draw.doubleSided].GetHandle();
                if (replay->buckets.empty() || pipeline != previousPipeline)
                {
                    auto& bucket = replay->AppendBucket();
                    bucket.representative = index;
                    bucket.offset = replayBytes;
                    previousPipeline = pipeline;
                }
                auto& bucket = replay->buckets.back();
                const auto first = static_cast<uint32_t>(replayWords.size() * 4);
                SceneIndirectRootLayout roots;
                const auto append = [&](uint32_t slot, RHIBufferSlice slice, bool cbv = false)
                {
                    uint64_t address{};
                    if (!device_->EncodeIndirectBuffer(slice, address) || (cbv && address % 256) ||
                        !roots.Append(slot, cbv))
                    {
                        return false;
                    }
                    replayWords.push_back(uint32_t(address));
                    replayWords.push_back(uint32_t(address >> 32));
                    return true;
                };
                const auto& layout = draw.program->layout;
                const unsigned ownerRoot = draw.program->visibleOwnerRoot;
                if (!append(0, draw.constants, true) || !append(1, RHIBufferSlice::Whole(draw.geometry->Buffer())) ||
                    !append(ownerRoot, draw.visibleOwner) ||
                    !append(draw.program->meshletRoot, draw.geometry->Meshlets()) ||
                    !append(draw.program->temporalPreviousRoot,
                            RHIBufferSlice::Whole(draw.previousGeometry->Buffer())) ||
                    !append(
                        draw.program->temporalPreviousRoot + 1,
                        replay->visibility->VisibleIds(replay->visibility->MeshletOutputOffset(draw.cameraMeshletBin),
                                                       draw.geometry->MeshletCount() + 1)) ||
                    (layout.uniformSlot && !append(*layout.uniformSlot, draw.bindings->uniforms, true)) ||
                    (layout.descriptorIndexSlot &&
                     !append(*layout.descriptorIndexSlot, draw.bindings->descriptorIndices, true)))
                {
                    return Fail(error, "LX temporal indirect root preparation failed.");
                }
                if (!roots.MatchOrInitialize(bucket.roots))
                {
                    return Fail(error, "LX temporal bucket root ABI mismatch.");
                }
                replayRecords.push_back({first, replayBytes, uint32_t(roots.count * 2),
                                         uint32_t(replay->visibility->ArgsOffset(draw.cameraMeshletBin))});
                replayBytes += uint32_t(roots.count * 8 + 12);
                ++bucket.count;
            }
            if (!replayRecords.empty())
            {
                if (!replay->Upload(replayWords, replayRecords, replayBytes, error))
                {
                    return false;
                }
                candidate->temporalStage.temporalCommands = std::move(replay);
            }
            {
                ce::profile_scope packetJoin{ce::marker<"MaterialScenePacketJoin">()};
                if (!packetPreparation.Join(error) || !packetErrors(std::span(cameraTasks).first(cameraTaskCount)) ||
                    !packetErrors(std::span(shadowTasks).first(shadowTaskCount)))
                {
                    return false;
                }
            }
            if (candidate->visibilityStage.materialCommands)
            {
                const std::pair shape{candidate->visibilityStage.materialCommands->commandCount,
                                      candidate->visibilityStage.materialCommands->buckets.size()};
                if (loggedMaterialBuckets_ != shape)
                {
                    loggedMaterialBuckets_ = shape;
                    std::printf("[meteor.material-buckets] commands=%u buckets=%zu cpuInstanceSubmissions=0\n",
                                shape.first, shape.second);
                }
            }
        }

        // Captures and tiled effects preserve each sorted layer boundary. Within
        // a layer, every geometry chunk shares material/scene constants and PSO;
        // only geometry, owner and its ordered meshlet list vary per mesh dispatch.
        auto indexed = m_preparationScratch->m_batches.Acquire();
        indexed->name = "METEOR.Scene.Commands.Layer";
        indexed->device = device_;
        indexed->pool = bufferPool_;
        indexed->pipeline = indirectCommands_.GetGeneration();
        indexed->visibility = candidate->finalCullStage.blendedMeshlets;
        ce::render_arena_vector<uint32_t> indexedWords{&scratch.m_storage->Resource(2)};
        ce::render_arena_vector<std::array<uint32_t, 4>> indexedRecords{&scratch.m_storage->Resource(2)};
        std::uint32_t indexedOutputBytes{};
        for (const auto [first, count] : candidate->inputRanges)
        {
            uint32_t bucketIndex = UINT32_MAX;
            for (size_t index = first; index < first + count; ++index)
            {
                auto& draw = candidate->draws[index];
                if (draw.blended && draw.program->hasSurface &&
                    (draw.program->hasSpecial || candidate->effectStage.lookup.layered->RequiresCapture()))
                {
                    const auto& list = candidate->finalCullStage.blendedMeshlets;
                    if (!list || !candidate->UsesMeshlets(draw))
                    {
                        return Fail(error, "METEOR layer requires its ordered blended meshlet list.");
                    }
                    if (bucketIndex == UINT32_MAX)
                    {
                        bucketIndex = static_cast<uint32_t>(indexed->buckets.size());
                        auto& bucket = indexed->AppendBucket();
                        bucket.representative = index;
                        bucket.offset = indexedOutputBytes;
                        bucket.roots = {1u, draw.program->visibleOwnerRoot, draw.program->meshletRoot,
                                        draw.program->temporalPreviousRoot + 1};
                    }
                    auto& bucket = indexed->buckets[bucketIndex];
                    const auto first = static_cast<uint32_t>(indexedWords.size() * 4);
                    for (const auto slice :
                         {RHIBufferSlice::Whole(draw.geometry->Buffer()), draw.visibleOwner, draw.geometry->Meshlets(),
                          list->VisibleIds(list->MeshletOutputOffset(draw.cameraMeshletBin),
                                           draw.geometry->MeshletCount() + 1)})
                    {
                        uint64_t address{};
                        if (!device_->EncodeIndirectBuffer(slice, address))
                        {
                            return Fail(error, "METEOR layer indirect root preparation failed.");
                        }
                        indexedWords.push_back(uint32_t(address));
                        indexedWords.push_back(uint32_t(address >> 32));
                    }
                    indexedRecords.push_back({first, indexedOutputBytes, uint32_t(bucket.roots.size() * 2),
                                              static_cast<uint32_t>(list->ArgsOffset(draw.cameraMeshletBin))});
                    indexedOutputBytes += static_cast<uint32_t>(bucket.roots.size() * 8 + 12);
                    draw.indexedBucket = bucketIndex;
                    draw.bucketRepresentative = bucket.count++ == 0;
                }
            }
        }
        if (!indexedRecords.empty())
        {
            if (!indexed->Upload(indexedWords, indexedRecords, indexedOutputBytes, error))
            {
                return false;
            }
            candidate->effectStage.layers.indexedCommands = std::move(indexed);
        }
        nextPreparationPhase(ce::marker<"MaterialSceneFinalizePrepare">());
        // The replay walks every caster vertex for three cascades. Sample at most
        // one prepared frame every two seconds so it cannot stall the render thread.
        static std::atomic<std::int64_t> lastAuditMs{};
        const auto nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count();
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
                std::printf(
                    "[meteor.meshlets.audit] frame=%llu cascade=%u draws=%llu visibleDraws=%llu meshlets=%llu "
                    "rejected=%llu triangles=%llu rejectedTriangles=%llu indexed=%u skinned=%u "
                    "scope=allLodsBeforeGpuSelection\n",
                    static_cast<unsigned long long>(context.frameId), cascade,
                    static_cast<unsigned long long>(audit.draws), static_cast<unsigned long long>(audit.visibleDraws),
                    static_cast<unsigned long long>(audit.meshlets), static_cast<unsigned long long>(audit.rejected),
                    static_cast<unsigned long long>(audit.triangles),
                    static_cast<unsigned long long>(audit.rejectedTriangles), indexed, skinned);
            }
            std::fflush(stdout);
        }
        if ((!sceneData->recordBuffer && !sceneData->Upload(sceneResidentBuffers_, candidate->input->View().sceneEpoch,
                                                            candidate->input->View().viewId, error)) ||
            !sceneData->PrepareBindings(*device_, error))
        {
            return false;
        }
        const auto preparePacketBindings = [&](const own::shared_owner<const IndirectMaterialBatch>& batch)
        {
            if (!batch)
            {
                return true;
            }
            RHIBindingTable table;
            if (!sceneData->CreatePacketBindings(*device_, batch->PacketBuffer(), batch->PacketCapacity(),
                                                 batch->RangeBuffer(), static_cast<uint32_t>(batch->buckets.size()),
                                                 table, error))
            {
                return false;
            }
            candidate->packetBindings.emplace(batch.operator->(), table);
            return true;
        };
        if (!preparePacketBindings(candidate->visibilityStage.materialCommands) ||
            !preparePacketBindings(candidate->occluderStage.depthCommands))
        {
            return false;
        }
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            if (!preparePacketBindings(candidate->shadowCacheStage.commands[cascade]) ||
                !preparePacketBindings(candidate->shadowOccluderStage.commands[cascade]) ||
                !preparePacketBindings(candidate->shadowRasterStage.shadowCommands[cascade]))
            {
                return false;
            }
        }
        if (const auto& work = candidate->materialWorkStage.materialWork; work)
        {
            if (budget.m_primitiveCache && ScenePrimitiveCacheFrame::SupportsPacketDispatch(*work))
            {
                if (!m_primitiveCacheResources)
                {
                    m_primitiveCacheResources = own::make_shared<ScenePrimitiveCacheResources>();
                }
                auto cache = own::make_shared<ScenePrimitiveCacheFrame>();
                if (!cache->Prepare(context, bufferPool_, m_primitiveCacheResources, sceneData, work,
                                    candidate->input->ViewProjection(), error))
                {
                    return false;
                }
                for (const auto& bucket : work->buckets)
                {
                    auto& draw = candidate->draws.at(bucket.representative);
                    const auto& program = *draw.program;
                    if (!m_primitiveCacheResources->PrepareReconstruction(
                            context, program.generation, program.gbuffer[draw.doubleSided], program.reconstructShader,
                            program.m_cachedSurfaceShader, false, draw.m_cachedSurfacePipeline, error) ||
                        !m_primitiveCacheResources->PrepareReconstruction(
                            context, program.generation, program.temporal[draw.doubleSided], program.reconstructShader,
                            program.m_cachedTemporalShader, true, draw.m_cachedTemporalPipeline, error))
                    {
                        return false;
                    }
                }
                candidate->m_primitiveCache = std::move(cache);
            }
            const auto& cache = candidate->m_primitiveCache;
            if (!sceneData->CreatePacketBindings(
                    *device_, work->work, work->PacketCapacity(), work->ranges,
                    static_cast<uint32_t>(work->buckets.size()), candidate->materialPacketBindings, error,
                    cache ? cache->Keys() : RHIBufferHandle{}, cache ? cache->Data() : RHIBufferHandle{}))
            {
                return false;
            }
        }
        if (!sceneData->CreatePacketBindings(*device_, sceneData->recordBuffer->Buffer(), 1,
                                             sceneData->recordBuffer->Buffer(), 1, candidate->legacyPacketBindings,
                                             error))
        {
            return false;
        }
        candidate->raster = own::make_shared<const RasterResources>(
            RasterResources{candidate->geometryStage.geometryTable, candidate->firstCullStage.cameraDepthMeshlets,
                            candidate->finalCullStage.cameraMeshlets, candidate->finalCullStage.blendedMeshlets,
                            candidate->effectStage.layers.indexedCommands, candidate->materialWorkStage.materialWork});
        try
        {
            if (!candidate->effectStage.layers.PrepareBlended(
                    *candidate, candidate->effectStage.lookup.layered, bool(candidate->effectStage.runtime.frame),
                    indirectCommands_.GetGeneration(), m_preparationScratch->m_batches, error))
            {
                return false;
            }
            candidate->CheckCurrent();
        }
        catch (const std::exception& exception)
        {
            return Fail(error, exception.what());
        }
        if (PassReadPlanReuseEnabled())
        {
            ce::profile_scope profile{ce::marker<"MaterialPassReadPlanAcquire">()};
            for (const auto& plan : m_passReadPlans)
            {
                if (plan && plan->Matches(candidate->draws, candidate->shadowDraws))
                {
                    ce::profile_scope hit{ce::marker<"MaterialPassReadPlanHit">()};
                    candidate->m_passReadPlan = plan;
                    break;
                }
            }
            if (!candidate->m_passReadPlan)
            {
                ce::profile_scope build{ce::marker<"MaterialPassReadPlanBuild">()};
                auto plan = own::make_shared<const PassReadPlan>(candidate->draws, candidate->shadowDraws);
                if (plan->StorageBytes() <= 128 * 1024)
                {
                    m_passReadPlans[m_nextPassReadPlan] = plan;
                    m_nextPassReadPlan = (m_nextPassReadPlan + 1) % m_passReadPlans.size();
                }
                candidate->m_passReadPlan = std::move(plan);
            }
        }
        nextPreparationPhase(ce::marker<"MaterialScenePublishAndRelease">());
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
        bufferPool_->PublishStatistics(static_cast<std::uint32_t>(context.frameId));
        // Resource/ticket destruction may call backend code; never under the
        // recording mutex, including completed entries replaced by another view.
        error.clear();
        return true;
    }

    // METEOR.Scene.Cull.First: the first camera list (completed history HZB or
    // frustum) and the depth commands the occluder stage consumes.
    SceneLayerWork SceneHost::DeclareLayerWork(EnhancedRenderGraph& graph, RGHandle layerList) const
    {
        const LayerStage empty;
        return (frame_ ? frame_->effectStage.layers : empty).Declare(frame_, RequireVersionedGraph(graph), layerList);
    }

    void SceneHost::DeclareSharedFirst(EnhancedRenderGraph& graph, RGHandle geometry) const
    {
        RequireVersionedGraph(graph);
        if (!frame_ || frame_->draws.empty())
        {
            return;
        }
        frame_->CheckCurrent(&graph);
        RequireStage(geometry, frame_->raster->geometry->GraphTable(graph), "Shared first culling requires Geometry.");
        std::array<own::shared_owner<const GpuGeometryVisibility::Frame>, 4> ready;
        std::size_t readyCount{};
        if (frame_->firstCullStage.cameraDepthMeshlets)
        {
            ready[readyCount++] = frame_->firstCullStage.cameraDepthMeshlets;
        }
        // 현재 카메라 깊이는 camera first 뒤에 만들어진다. 예측을 켠 경우에만 shadow first를 분리한다.
        if (!frame_->shadowFirstCullStage.m_cameraPredictionView)
        {
            for (const auto& shadow : frame_->shadowFirstCullStage.meshlets)
            {
                if (shadow)
                {
                    ready[readyCount++] = shadow;
                }
            }
        }
        if (readyCount)
        {
            GpuGeometryVisibility::Frame::DeclareFirstStageBatch(graph, std::span{ready}.first(readyCount),
                                                                 "METEOR.Shared.First");
        }
    }

    SceneFirstCullOutputs SceneHost::DeclareFirstCull(EnhancedRenderGraph& graph, RGHandle geometry) const
    {
        DeclareSharedFirst(graph, geometry);
        const FirstCullStage empty;
        const auto& stage = frame_ ? frame_->firstCullStage : empty;
        return stage.DeclareFirstCull(frame_, RequireVersionedGraph(graph), geometry);
    }

    // METEOR.Scene.Occluder: first list -> HW/SW atomic keys -> depth-only
    // resolve. Same producer contract as the final surface; no depth-only raster.
    RGHandle SceneHost::DeclareOccluder(EnhancedRenderGraph& graph, RGHandle geometry,
                                        const SceneFirstCullOutputs& first) const
    {
        const OccluderStage empty;
        const auto& stage = frame_ ? frame_->occluderStage : empty;
        return stage.DeclareOccluder(frame_, RequireVersionedGraph(graph), geometry, first);
    }

    // METEOR.Scene.Cull.Final: current HZB from the occluder depth, instance
    // and recovery culling, then every command producer whose input is now final.
    SceneFinalCullOutputs SceneHost::DeclareFinalCull(EnhancedRenderGraph& graph, RGHandle geometry,
                                                      RGHandle occluderDepth) const
    {
        const FinalCullStage empty;
        const auto& stage = frame_ ? frame_->finalCullStage : empty;
        return stage.DeclareFinalCull(frame_, RequireVersionedGraph(graph), geometry, occluderDepth,
                                      frame_ ? frame_->occluderStage.occluderVisibility : nullptr);
    }

    void SceneHost::DeclareHardwareVisibility(EnhancedRenderGraph& graph, const own::shared_owner<const Frame>& frame,
                                              const own::shared_owner<const SceneHybridVisibility>& hybrid,
                                              const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                              const own::shared_owner<const IndirectMaterialBatch>& commands,
                                              bool occluder, RGHandle commandInput)
    {
        if (!hybrid || !list || !commands)
        {
            throw std::runtime_error("METEOR HW visibility requires its stage list, commands and atomic keys.");
        }
        auto uses = graph.CreateDeclarationUsages();
        hybrid->AddRasterUses(graph, uses);
        list->AddVisibleIdsRead(graph, uses);
        commands->AddRead(graph, uses, commandInput);
        frame->sceneData->AddRead(graph, uses);
        uses.push_back(
            {frame->raster->geometry->GraphTable(graph), RHIResourceState::ShaderResource, RGAccessMode::Read});

        frame->AddPassReadPlan(graph, uses, PassReadPlan::Pass::Surface, RHIResourceState::ShaderResource);
        NormalizeSceneReads(graph, uses);
        const auto bucketCount = static_cast<uint32_t>(commands->buckets.size());
        const auto pass = graph.AddSplitPass(
            hybrid->name + ".HW", std::move(uses),
            [frame = frame, hybrid, list, commands, occluder](const auto& execution, uint32_t slice,
                                                              uint32_t sliceCount)
            {
                frame->CheckCurrent(execution.graph);
                const bool audit = !occluder && frame->geometryRouteAuditEnabled;
                auto& observations = frame->recordedGeometryRoutes[slice];
                if (audit)
                {
                    observations.clear();
                }
                auto& encoder = *execution.encoder;
                if (!encoder.BeginRasterWithoutAttachments(frame->input->View().width, frame->input->View().height))
                {
                    throw std::runtime_error("METEOR HW visibility requires UAV-only raster support.");
                }
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                const auto first = commands->buckets.size() * slice / sliceCount;
                const auto last = commands->buckets.size() * (slice + 1) / sliceCount;
                for (auto bucket = first; bucket < last; ++bucket)
                {
                    const auto& draw = frame->draws[commands->buckets[bucket].representative];
                    if (!draw.program->hasSurface || draw.program->hasTransmission || draw.blended)
                    {
                        continue;
                    }
                    if (!frame->UsesMeshlets(draw))
                    {
                        throw std::runtime_error("METEOR HW visibility requires cooked meshlets; no indexed route.");
                    }
                    const auto sourcePipeline = draw.program->visibilitySurface[draw.doubleSided].GetHandle();
                    const auto recordedPipeline = frame->RasterPipeline(draw, sourcePipeline);
                    encoder.SetPipeline(RHIBindPoint::Graphics, recordedPipeline);
                    std::string error;
                    if (!RenderBindingCache::Bind(*frame->device, encoder, RHIBindPoint::Graphics, *draw.bindings,
                                                  error))
                    {
                        throw std::runtime_error(error);
                    }
                    frame->BindPacketResources(encoder, draw, frame->packetBindings.at(commands.operator->()),
                                               draw.program->packetRoots);
                    encoder.SetRootBuffer(RHIBindPoint::Graphics, draw.program->rasterRouteRoot, hybrid->RouteSlice());
                    if (!encoder.SetRootUnorderedBuffer(RHIBindPoint::Graphics, draw.program->visibilityKeyRoot,
                                                        hybrid->KeySlice()))
                    {
                        throw std::runtime_error("METEOR HW visibility key binding failed.");
                    }
                    commands->Draw(encoder, bucket, draw.program->packetRoots.selector);
                    if (!audit)
                    {
                        continue;
                    }
                    for (const auto& submitted : frame->draws)
                    {
                        if (submitted.materialBucket != draw.materialBucket || submitted.blended)
                        {
                            continue;
                        }
                        const auto& source = frame->input->Draws()[submitted.inputIndex];
                        observations.push_back({source.geometryKey, source.temporalObjectId, source.temporalInstanceId,
                                                sourcePipeline.id, recordedPipeline.id,
                                                submitted.geometry->MeshletCount(), true, true});
                    }
                }
            },
            FrameInput::RasterRecordingSlices(bucketCount), false, bucketCount);
        // GPU 목록/명령은 제출 순서로 동기화한다. CPU 입력은 밀봉되어 있고,
        // final HW만 slice별 audit에 쓰며 관측자는 전체 recording join 뒤에 읽는다.
        graph.DeclareRecordingIndependent(pass);
    }

    own::shared_owner<const GpuGeometryVisibility::Frame> SceneHost::CameraMeshletWork() const
    {
        return frame_ ? frame_->finalCullStage.cameraMeshlets : nullptr;
    }

    own::shared_owner<const GpuGeometryVisibility::Frame> SceneHost::CameraDepthMeshletWork() const
    {
        return frame_ ? frame_->firstCullStage.cameraDepthMeshlets : nullptr;
    }

    own::shared_owner<const SceneMaterialWork> SceneHost::MaterialWork() const
    {
        return frame_ ? frame_->materialWorkStage.materialWork : nullptr;
    }

    own::shared_owner<const SceneFrameGeometry> SceneHost::FrameGeometry() const
    {
        return frame_ ? frame_->geometryStage.geometryTable : nullptr;
    }

    own::shared_owner<const SceneHybridVisibility> SceneHost::HybridVisibility() const
    {
        return frame_ ? frame_->visibilityStage.hybridVisibility : nullptr;
    }
    own::shared_owner<const SceneHybridVisibility> SceneHost::OccluderVisibility() const
    {
        return frame_ ? frame_->occluderStage.occluderVisibility : nullptr;
    }

    RGHandle SceneHost::DeclareGeometry(EnhancedRenderGraph& graph) const
    {
        const GeometryStage empty;
        const auto& stage = frame_ ? frame_->geometryStage : empty;
        return stage.DeclareGeometry(frame_, RequireVersionedGraph(graph));
    }

    uint32_t SceneHost::PreparedMeshletDrawCount() const
    {
        if (!frame_)
        {
            return 0;
        }
        return static_cast<uint32_t>(
            std::ranges::count_if(frame_->draws, [&](const auto& draw) { return frame_->UsesMeshlets(draw); }));
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
            for (const auto& slice : frame_->recordedGeometryRoutes)
            {
                result.draws.insert(result.draws.end(), slice.begin(), slice.end());
            }
        }
        return result;
    }

    std::array<uint32_t, 3> SceneHost::ShadowBucketSubmissionCounts() const
    {
        std::array<uint32_t, 3> result{};
        if (frame_)
        {
            for (unsigned cascade = 0; cascade < result.size(); ++cascade)
            {
                for (uint32_t slice = 0; slice < FrameInput::kMaxRasterRecordingSlices; ++slice)
                {
                    result[cascade] += frame_->shadowRecordingStats[cascade * 2][slice].buckets +
                                       frame_->shadowRecordingStats[cascade * 2 + 1][slice].buckets;
                }
            }
        }
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
                                   frame_->indexedLogicalCommands.load(std::memory_order_relaxed)}
                      : std::array<uint32_t, 2>{};
    }

    uint32_t SceneHost::ShadowDrawCount() const
    {
        uint32_t result{};
        for (const auto count : RecordedShadowMeshletDrawCounts())
        {
            result += count;
        }
        return result;
    }

    std::array<uint32_t, 3> SceneHost::RecordedShadowMeshletDrawCounts() const
    {
        std::array<uint32_t, 3> counts{};
        if (frame_)
        {
            for (unsigned cascade = 0; cascade < counts.size(); ++cascade)
            {
                for (uint32_t slice = 0; slice < FrameInput::kMaxRasterRecordingSlices; ++slice)
                {
                    counts[cascade] += frame_->shadowRecordingStats[cascade * 2][slice].commands +
                                       frame_->shadowRecordingStats[cascade * 2 + 1][slice].commands;
                }
            }
        }
        return counts;
    }

    std::vector<SceneHost::ShadowMeshletOutput> SceneHost::ShadowMeshletOutputs() const
    {
        std::vector<ShadowMeshletOutput> result;
        if (frame_)
        {
            for (const auto& draw : frame_->shadowDraws)
            {
                for (unsigned cascade = 0; cascade < 3; ++cascade)
                {
                    if (draw.shadowMeshlets[cascade])
                    {
                        result.push_back({cascade, draw.shadowMeshletBin, draw.shadowMeshlets[cascade]});
                    }
                }
            }
        }
        return result;
    }

    GpuGeometryVisibility::PreparedStats SceneHost::CameraVisibilityStats() const
    {
        GpuGeometryVisibility::PreparedStats total{};
        if (frame_)
        {
            for (const auto& list : {frame_->finalCullStage.cameraMeshlets, frame_->finalCullStage.blendedMeshlets})
            {
                if (list)
                {
                    const auto stats = list->GetPreparedStats();
                    total.candidateCount += stats.candidateCount;
                    total.compactedBins += stats.compactedBins;
                    total.preservedBins += stats.preservedBins;
                    total.conservativeCandidates += stats.conservativeCandidates;
                }
            }
        }
        return total;
    }

    std::array<GpuGeometryVisibility::PreparedStats, 3> SceneHost::ShadowVisibilityStats() const
    {
        std::array<GpuGeometryVisibility::PreparedStats, 3> result{};
        if (frame_ && !frame_->shadowDraws.empty())
        {
            for (unsigned c = 0; c < 3; ++c)
            {
                if (const auto& list = frame_->shadowDraws.front().shadowMeshlets[c])
                {
                    result[c] = list->GetPreparedStats();
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
                counts[cascade] += draw.shadowMeshlets[cascade] ? 1u : 0u;
            }
        }
        return counts;
    }

    SceneHost::ShadowCacheStats SceneHost::CurrentShadowCacheStats() const
    {
        ShadowCacheStats stats;
        if (!frame_ || !frame_->shadowCacheStage.cache)
        {
            return stats;
        }
        stats.hit = frame_->shadowCacheStage.hit;
        stats.sourceFrame = frame_->shadowCacheStage.cache->frameId;
        stats.bytes = frame_->shadowCacheStage.cache->bytes;
        for (const auto& draw : frame_->shadowDraws)
        {
            if (draw.shadowEnabled)
            {
                if (draw.staticShadow)
                {
                    ++stats.staticCasters;
                }
                else
                {
                    ++stats.dynamicCasters;
                }
            }
        }
        return stats;
    }

    own::shared_owner<const GpuGeometryVisibility::Frame> SceneHost::ShadowFirstMeshletWork(unsigned cascade) const
    {
        return frame_ && cascade < 3 ? frame_->shadowFirstCullStage.meshlets[cascade] : nullptr;
    }

    bool SceneHost::UsesShadowDepthPrediction() const
    {
        return frame_ && frame_->shadowFirstCullStage.m_cameraPredictionView.has_value() &&
               frame_->shadowFirstCullStage.meshlets[0];
    }

    SceneShadowResources SceneHost::DeclareShadowStaticCache(EnhancedRenderGraph& graph, RGHandle geometry) const
    {
        const ShadowCacheStage empty;
        return (frame_ ? frame_->shadowCacheStage : empty).Declare(frame_, RequireVersionedGraph(graph), geometry);
    }

    SceneShadowResources SceneHost::DeclareShadowFirstCull(EnhancedRenderGraph& graph, RGHandle geometry,
                                                           RGHandle cameraOccluderDepth) const
    {
        DeclareSharedFirst(graph, geometry);
        const ShadowFirstCullStage empty;
        return (frame_ ? frame_->shadowFirstCullStage : empty)
            .Declare(frame_, RequireVersionedGraph(graph), geometry, cameraOccluderDepth);
    }

    SceneShadowResources SceneHost::DeclareShadowOccluder(EnhancedRenderGraph& graph, RGHandle geometry,
                                                          const SceneShadowResources& cache,
                                                          const SceneShadowResources& first) const
    {
        RequireVersionedGraph(graph);
        if (frame_ && frame_->shadow)
        {
            frame_->CheckCurrent(&graph);
            RequireStage(geometry, frame_->raster->geometry->GraphTable(graph), "Shadow.Occluder requires Geometry.");
            RequireShadowResources(cache, frame_->shadowCacheStage.outputs,
                                   "Shadow.Occluder requires the static depth versions.");
            RequireShadowResources(first, frame_->shadowFirstCullStage.outputs,
                                   "Shadow.Occluder requires the first-list versions.");
            if (frame_->shadowCacheStage.cache && (!cache[0].IsValid() || !first[0].IsValid()))
            {
                throw std::runtime_error("Shadow.Occluder requires both explicit producers.");
            }
        }
        const ShadowOccluderStage empty;
        return (frame_ ? frame_->shadowOccluderStage : empty).Declare(frame_, graph, cache, first);
    }

    SceneShadowResources SceneHost::DeclareShadowFinalCull(EnhancedRenderGraph& graph, RGHandle geometry,
                                                           const SceneShadowResources& depths) const
    {
        RequireVersionedGraph(graph);
        if (frame_ && frame_->shadow)
        {
            frame_->CheckCurrent(&graph);
            RequireStage(geometry, frame_->raster->geometry->GraphTable(graph), "Shadow.Cull.Final requires Geometry.");
            RequireShadowResources(depths, frame_->shadowOccluderStage.outputs,
                                   "Shadow.Cull.Final requires current occluder depths.");
            if (frame_->shadowCacheStage.cache && !depths[0].IsValid())
            {
                throw std::runtime_error("Shadow.Cull.Final cannot declare a missing occluder producer.");
            }
        }
        const ShadowFinalCullStage empty;
        return (frame_ ? frame_->shadowFinalCullStage : empty).Declare(frame_, graph, depths);
    }

    RGHandle SceneHost::DeclareShadowRaster(EnhancedRenderGraph& graph, RGHandle geometry,
                                            const SceneShadowResources& cache, const SceneShadowResources& lists,
                                            RGHandle shadowMap) const
    {
        RequireVersionedGraph(graph);
        if (frame_ && frame_->shadow)
        {
            frame_->CheckCurrent(&graph);
            RequireStage(geometry, frame_->raster->geometry->GraphTable(graph), "Shadow.Raster requires Geometry.");
            RequireShadowResources(cache, frame_->shadowCacheStage.outputs,
                                   "Shadow.Raster requires static cache depths.");
            RequireShadowResources(lists, frame_->shadowFinalCullStage.outputs,
                                   "Shadow.Raster requires final culling lists.");
            if (frame_->shadowCacheStage.cache && (!cache[0].IsValid() || !lists[0].IsValid()))
            {
                throw std::runtime_error("Shadow.Raster requires both explicit producers.");
            }
        }
        const ShadowRasterStage empty;
        return (frame_ ? frame_->shadowRasterStage : empty).Declare(frame_, graph, cache, lists, shadowMap);
    }

    void SceneHost::DeclareShadowHistory(EnhancedRenderGraph& graph, RGHandle shadowMap) const
    {
        const ShadowHistoryStage empty;
        (frame_ ? frame_->shadowHistoryStage : empty)
            .Declare(frame_, RequireVersionedGraph(graph), frame_ ? frame_->shadowRasterStage.output : RGHandle{},
                     shadowMap);
    }

    // Standalone fixtures use the exact same six producers as the live nodes.
    RGHandle SceneHost::DeclareShadow(EnhancedRenderGraph& graph, RGHandle geometry, RGHandle shadowMap,
                                      RGHandle cameraOccluderDepth) const
    {
        const auto cache = DeclareShadowStaticCache(graph, geometry);
        const auto first = DeclareShadowFirstCull(graph, geometry, cameraOccluderDepth);
        const auto depth = DeclareShadowOccluder(graph, geometry, cache, first);
        const auto final = DeclareShadowFinalCull(graph, geometry, depth);
        const auto output = DeclareShadowRaster(graph, geometry, cache, final, shadowMap);
        DeclareShadowHistory(graph, output);
        return output;
    }

    std::array<RGHandle, 5> SceneHost::DeclareTemporal(EnhancedRenderGraph& graph, RGHandle visibility,
                                                       const std::array<RGHandle, 2>& material, RGHandle layerList,
                                                       std::array<RGHandle, 5> targets) const
    {
        const TemporalStage empty;
        const auto& stage = frame_ ? frame_->temporalStage : empty;
        return stage.DeclareTemporal(frame_, RequireVersionedGraph(graph),
                                     frame_ ? frame_->visibilityStage.targets.visibility : RGHandle{}, visibility,
                                     material, layerList, targets);
    }

    SceneSurfaceTargets::Outputs SceneHost::DeclareVisibility(EnhancedRenderGraph& graph,
                                                              const SceneFinalCullOutputs& cull,
                                                              const SceneSurfaceTargets::Outputs& targets) const
    {
        const VisibilityStage empty;
        const auto& stage = frame_ ? frame_->visibilityStage : empty;
        return stage.Declare(frame_, RequireVersionedGraph(graph), cull, targets);
    }

    std::array<RGHandle, 2> SceneHost::DeclareMaterialWork(EnhancedRenderGraph& graph, RGHandle geometry,
                                                           RGHandle visibility) const
    {
        RequireStage(geometry, frame_ ? frame_->raster->geometry->GraphTable(graph) : RGHandle{},
                     "MaterialWork requires the published Geometry table.");
        const MaterialWorkStage empty;
        const auto& stage = frame_ ? frame_->materialWorkStage : empty;
        return stage.Declare(frame_, RequireVersionedGraph(graph),
                             frame_ ? frame_->visibilityStage.targets.visibility : RGHandle{}, visibility);
    }

    SceneSurfaceTargets::Outputs SceneHost::DeclareSurfaceResolve(EnhancedRenderGraph& graph, RGHandle geometry,
                                                                  const SceneSurfaceTargets::Outputs& targets,
                                                                  const std::array<RGHandle, 2>& material) const
    {
        RequireStage(geometry, frame_ ? frame_->raster->geometry->GraphTable(graph) : RGHandle{},
                     "SurfaceResolve requires the published Geometry table.");
        if (frame_ && !frame_->draws.empty())
        {
            RequireStage(targets.depth, frame_->visibilityStage.targets.depth,
                         "SurfaceResolve requires the published Visibility depth.");
        }
        const SurfaceResolveStage empty;
        const auto& stage = frame_ ? frame_->surfaceResolveStage : empty;
        return stage.Declare(frame_, RequireVersionedGraph(graph),
                             frame_ ? frame_->visibilityStage.targets.visibility : RGHandle{}, targets, material);
    }

    SceneSurfaceTargets::Outputs SceneHost::DeclareGBuffer(EnhancedRenderGraph& graph,
                                                           const SceneFinalCullOutputs& cull,
                                                           const SceneSurfaceTargets::Outputs& incoming) const
    {
        const auto visibility = DeclareVisibility(graph, cull, incoming);
        const auto geometry = frame_ ? frame_->raster->geometry->GraphTable(graph) : RGHandle{};
        const auto material = DeclareMaterialWork(graph, geometry, visibility.visibility);
        return DeclareSurfaceResolve(graph, geometry, visibility, material);
    }

    void SceneHost::DeclareHistory(EnhancedRenderGraph& graph, RGHandle depth) const
    {
        const HistoryStage empty;
        const auto& stage = frame_ ? frame_->historyStage : empty;
        stage.DeclareHistory(frame_, RequireVersionedGraph(graph),
                             frame_ ? frame_->visibilityStage.targets.depth : RGHandle{}, depth);
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
        const auto output = RequireVersionedGraph(graph).Write(graph.CreateTexture(description));
        graph.AddPass("METEOR.Scene.HdrClear", {{output, RHIResourceState::RenderTarget, RGAccessMode::Write}},
                      [device = context.resources, output](const auto& execution)
                      {
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
                                     const SceneColorInputs& commands, RGHandle lighting, RGHandle ambientOcclusion,
                                     RGHandle shadowMap) const
    {
        const EffectStage empty;
        const auto& stage = frame_ ? frame_->effectStage : empty;
        return stage.DeclareColor(frame_, RequireVersionedGraph(graph),
                                  frame_ ? frame_->surfaceResolveStage.gbuffer : SceneSurfaceTargets::Outputs{}, inputs,
                                  commands, lighting, ambientOcclusion, shadowMap);
    }

    std::vector<SceneHost::ForwardDraw> SceneHost::BlendedDraws() const
    {
        const EffectStage empty;
        const auto& stage = frame_ ? frame_->effectStage : empty;
        return stage.BlendedDraws(frame_);
    }

    RGHandle SceneHost::DeclareBlended(EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices,
                                       const SceneLayerWork& commands, RGHandle lighting, RGHandle shadowMap,
                                       const EnhancedForwardLighting& forward) const
    {
        const EffectStage empty;
        const auto& stage = frame_ ? frame_->effectStage : empty;
        return stage.DeclareBlended(frame_, RequireVersionedGraph(graph),
                                    frame_ ? frame_->surfaceResolveStage.gbuffer : SceneSurfaceTargets::Outputs{},
                                    indices, commands, lighting, shadowMap, forward);
    }

    RGHandle SceneHost::DeclareBlendedStream(EnhancedRenderGraph& graph, const SceneLayerWork& commands,
                                             RGHandle lighting, RGHandle shadowMap,
                                             const EnhancedForwardLighting& tiles) const
    {
        const EffectStage empty;
        const auto& stage = frame_ ? frame_->effectStage : empty;
        return stage.DeclareBlendedStream(frame_, RequireVersionedGraph(graph),
                                          frame_ ? frame_->surfaceResolveStage.gbuffer : SceneSurfaceTargets::Outputs{},
                                          commands, lighting, shadowMap, tiles);
    }

    RGHandle SceneHost::DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth,
                                      RGHandle shadowMap) const
    {
        const VolumeStage empty;
        return (frame_ ? frame_->effectStage.volume : empty)
            .Declare(frame_, RequireVersionedGraph(graph), frame_ ? frame_->effectStage.colorOutput : RGHandle{},
                     lighting, depth, shadowMap);
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
                found->second.completion != completion.value || frame_->input->View().frameId != frameId)
            {
                return Fail(error, "METEOR Scene material publication needs its exact successful native submission.");
            }
        }
        bool pending = false;
        if (ticket.IsValid())
        {
            const auto identity = ticket.GetIdentity();
            if (identity.frameId != frameId || identity.recordingId != frame_->recording ||
                identity.completion.value != completion.value)
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
            if (draw.previousGeometry && draw.previousGeometry.operator->() != draw.geometry.operator->())
            {
                draw.previousGeometry->MarkSubmitted(completion);
            }
        }
        for (const auto& draw : frame_->shadowDraws)
        {
            draw.geometry->MarkSubmitted(completion);
        }
        if (frame_->effectStage.runtime.frame)
        {
            frame_->effectStage.runtime.frame->MarkSubmitted(completion);
        }
        std::string firstError;
        bool tracked = lookup_.TrackAcceptedSubmission(*frame_->effectStage.lookup.opaque, completion, firstError);
        if (frame_->effectStage.lookup.layered)
        {
            std::string alphaError;
            if (!lookup_.TrackAcceptedSubmission(*frame_->effectStage.lookup.layered, completion, alphaError))
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
        if (frame->effectStage.runtime.frame)
        {
            frame->effectStage.runtime.frame->ConfirmSubmitted(completion);
        }
        const auto forwardCount = std::ranges::count_if(
            frame->input->Draws(),
            [](const auto& draw)
            {
                const auto& p = draw.material->generation->cooked.product.program;
                return p.surface && (draw.queue == SceneCoverage::Blended || (p.features & 0x0800u) != 0);
            });
        if (frame->effectStage.lookup.layered &&
            frame->effectStage.layers.blendedOutputs.size() != static_cast<std::size_t>(forwardCount))
        {
            return Fail(error, "METEOR alpha cache publication requires the complete blended stream.");
        }
        if (frame->effectStage.lookup.layered &&
            !lookup_.PublishSubmitted(*frame->effectStage.lookup.layered, frame->input->View().frameId, completion,
                                      error))
        {
            return false;
        }
        if (!lookup_.PublishSubmitted(*frame->effectStage.lookup.opaque, frame->input->View().frameId, completion,
                                      error))
        {
            return false;
        }
        const auto& view = frame->input->View();
        if (frame->historyStage.nextCameraHistory &&
            frame->historyStage.nextCameraHistory->HistoryOwner()->recorded.load())
        {
            auto history = frame->historyStage.nextCameraHistory->HistoryOwner();
            history->completion = completion.value;
            auto& histories = cameraHistories_[{view.sceneEpoch, view.viewId}];
            if (std::ranges::none_of(histories, [&](const auto& item)
                                     { return item.pyramid.operator->() == history.operator->(); }))
            {
                auto historyCamera = view.camera;
                if (view.temporalFrame.camera.valid)
                {
                    std::memcpy(&historyCamera.projection, view.temporalFrame.camera.projectionMatrix.data(),
                                sizeof(historyCamera.projection));
                }
                histories.push_back({std::move(history), historyCamera, view.temporalFrame.historyRevision});
            }
        }
        if (frame->shadowCacheStage.cache && !frame->shadowCacheStage.hit &&
            frame->shadowCacheStage.cache->recordedMask.load() == 7u)
        {
            auto cache = frame->shadowCacheStage.cache;
            cache->completion = completion.value;
            const auto key = std::pair{view.sceneEpoch, view.viewId};
            auto& cached = shadowCaches_[key];
            if (std::ranges::none_of(cached, [&](const auto& item) { return item.operator->() == cache.operator->(); }))
            {
                cached.push_back(std::move(cache));
            }
        }
        ShadowHistory shadowHistory;
        bool completeHistory = true;
        for (unsigned cascade = 0; cascade < 3; ++cascade)
        {
            const auto& pyramid = frame->shadowHistoryStage.next[cascade];
            if (!pyramid || !pyramid->HistoryOwner()->recorded.load())
            {
                completeHistory = false;
                break;
            }
            shadowHistory.cascades[cascade] = pyramid->HistoryOwner();
            shadowHistory.cascades[cascade]->completion = completion.value;
        }
        if (completeHistory)
        {
            shadowHistory.frameId = view.frameId;
            shadowHistory.revision = view.historyRevision;
            shadowHistory.temporalRevision = view.temporalFrame.historyRevision;
            const auto key = std::pair{view.sceneEpoch, view.viewId};
            auto& histories = shadowHistories_[key];
            if (std::ranges::none_of(histories,
                                     [&](const auto& item) { return item.frameId == shadowHistory.frameId; }))
            {
                histories.push_back(std::move(shadowHistory));
            }
        }
        BoundShadowViews(shadowCaches_, std::pair{view.sceneEpoch, view.viewId});
        BoundShadowViews(shadowHistories_, std::pair{view.sceneEpoch, view.viewId});
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
        sceneResidentBuffers_.ConfirmSubmitted(frame->recording, completion);
        for (const auto& draw : frame->input->Draws())
            if (draw.material->editorPreview && draw.material->editorPreviewSubmitted)
                draw.material->editorPreviewSubmitted->store(true, std::memory_order_release);
        static const bool tracePlacement = [] {
            char* value{};
            std::size_t length{};
            _dupenv_s(&value, &length, "CE_TRACE_SHADER_COMPILE");
            const bool enabled = value && *value;
            std::free(value);
            return enabled;
        }();
        if (tracePlacement)
        {
            std::map<std::uint64_t, std::pair<bool, double>> placements;
            for (const auto& draw : frame->input->Draws())
            {
                if (!draw.material->editorPlacementId) continue;
                auto& phase = placements[draw.material->editorPlacementId];
                phase.first |= draw.material->editorPreview;
                phase.second = draw.material->editorPlacementStartMs;
            }
            for (const auto& [id, phase] : placements)
                if (loggedPlacementPhases_.insert({id, phase.first}).second)
                {
                    const auto now = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now().time_since_epoch()).count();
                    std::printf("[model.display] request=%llu phase=%s submittedMs=%.3f frame=%llu\n",
                        static_cast<unsigned long long>(id), phase.first ? "preview" : "original", now - phase.second,
                        static_cast<unsigned long long>(frame->input->View().frameId));
                    std::fflush(stdout);
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
        return HasDraws() ? frame_->effectStage.lookup.opaque->Statistics() : RHIBufferHandle{};
    }

    RGHandle SceneHost::GraphLookupStatistics(const EnhancedRenderGraph& graph) const
    {
        return HasDraws() ? frame_->effectStage.lookup.opaque->GraphStatistics(graph) : RGHandle{};
    }

    own::shared_owner<const SceneLookupFrame> SceneHost::LookupFrame() const
    {
        return HasDraws() ? frame_->effectStage.lookup.opaque : nullptr;
    }

    own::shared_owner<const SceneLookupFrame> SceneHost::ForwardLookupFrame() const
    {
        return HasDraws() ? frame_->effectStage.lookup.layered : nullptr;
    }

    SceneSurfaceTargets::Outputs SceneHost::ForwardSurfaceOutputs() const
    {
        return HasDraws() ? frame_->effectStage.layers.forwardGbuffer : SceneSurfaceTargets::Outputs{};
    }

    own::shared_owner<const SceneSubsurfaceFrame> SceneHost::SubsurfaceFrame() const
    {
        return HasDraws() ? frame_->effectStage.subsurface : nullptr;
    }

    own::shared_owner<const SceneRefractionFrame> SceneHost::RefractionFrame() const
    {
        return HasDraws() ? frame_->effectStage.refraction : nullptr;
    }

    own::shared_owner<const SceneVolumeFrame> SceneHost::VolumeFrame() const
    {
        return HasDraws() ? frame_->effectStage.volume.frame : nullptr;
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
        m_passReadPlans = {};
        m_nextPassReadPlan = 0;
        cameraHistories_.clear();
        shadowCaches_.clear();
        shadowHistories_.clear();
        shadowCacheAllocations_.clear();
        shadowOcclusion_.ShutdownAfterIdle();
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
        packetPipelines_ = {};
        hybridPipelines_ = {};
        shadowMerge_ = {};
        materialWorkPipelines_ = {};
        m_primitiveCacheResources.reset();
        geometry_ = {};
        if (m_preparationScratch)
        {
            m_preparationScratch->m_batches.ClearAfterIdle();
        }
        sceneResidentBuffers_.ShutdownAfterIdle();
        if (bufferPool_)
        {
            bufferPool_->Close();
        }
        bufferPool_.reset();
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
        loggedPlacementPhases_.clear();
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
        owner.completion =
            owner.accepted
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
