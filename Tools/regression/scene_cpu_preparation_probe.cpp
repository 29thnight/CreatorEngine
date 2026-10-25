#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
// 봉인 입력/RHI 경계만 모형으로 두고 실제 CPU 본문과 스케줄러의 순서·수명을 검증한다.
#include "Engine/RenderEngine/MaterialGraphPreparationTasks.h"
#include "Engine/RenderEngine/MaterialGraphGeometryContract.h"
#include "Engine/RenderEngine/MaterialGraphVisibilityContract.h"
#include <cstdlib>
#include <iomanip>
#include <iostream>

namespace
{
    std::atomic<bool> count_allocations{};
    std::atomic<std::uint64_t> allocations{};
} // namespace

void* operator new(std::size_t bytes)
{
    if (count_allocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    if (auto* memory = std::malloc(bytes ? bytes : 1))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes)
{
    return ::operator new(bytes);
}
void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept
{
    return ::operator new(bytes, tag);
}
void operator delete(void* memory) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
void* operator new(std::size_t bytes, std::align_val_t alignment)
{
    if (count_allocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
    void* memory{};
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), bytes ? bytes : 1) == 0)
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes, std::align_val_t alignment)
{
    return ::operator new(bytes, alignment);
}
void* operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(bytes, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t& tag) noexcept
{
    return ::operator new(bytes, alignment, tag);
}
void operator delete(void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept
{
    std::free(memory);
}
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(memory);
}
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    std::free(memory);
}

namespace math
{
    struct alignas(16) vector4
    {
        float x{}, y{}, z{}, w{};
        vector4() = default;
        vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    };
    using color = vector4;
    struct alignas(16) matrix4x4
    {
        float m[4][4]{};
    };
    matrix4x4 transpose(const matrix4x4& source)
    {
        matrix4x4 result;
        for (unsigned row = 0; row < 4; ++row)
        {
            for (unsigned column = 0; column < 4; ++column)
            {
                result.m[row][column] = source.m[column][row];
            }
        }
        return result;
    }
} // namespace math

namespace own
{
    template<class T>
    using shared_owner = std::shared_ptr<T>;
}

using material_graph::SceneGeometryIdentity;
using material_graph::ScenePreparationTasks;
using material_graph::SceneVisibilityContract;

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }
    bool Fail(std::string& error, std::string message)
    {
        error = std::move(message);
        return false;
    }
    template<class Predicate>
    void await_ready(Predicate&& ready)
    {
        while (!ready())
        {
            std::this_thread::yield();
        }
    }

    struct MetadataGate
    {
        std::atomic<std::uint64_t> m_reads{};
        std::atomic<bool> m_entered{}, m_release{true};
        void Read()
        {
            m_reads.fetch_add(1, std::memory_order_relaxed);
            m_entered.store(true, std::memory_order_release);
            await_ready([&] { return m_release.load(std::memory_order_acquire); });
        }
    };

    struct MeshSurfaceBatch
    {
        std::uint32_t m_meshlets{}, m_id{};
        std::uint32_t MeshletCount() const { return m_meshlets; }
    };

    struct MeshSurfaceInput
    {
        struct HierarchyData
        {
            std::uint32_t groupCount{};
        };
        struct GeometryData
        {
            std::uint32_t sourceLodIndex{};
        };
        math::matrix4x4 m_world;
        HierarchyData m_hierarchy;
        GeometryData m_geometry;
        std::uint32_t m_primitives{}, m_vertices{};
        std::shared_ptr<const MeshSurfaceBatch> m_batch;
        std::shared_ptr<MetadataGate> m_gate;
        const math::matrix4x4& World() const
        {
            if (m_gate)
            {
                m_gate->Read();
            }
            return m_world;
        }
        std::uint32_t Count() const { return m_vertices; }
        const HierarchyData& Hierarchy() const { return m_hierarchy; }
        const GeometryData& Geometry() const { return m_geometry; }
        std::uint32_t SourcePrimitiveCount() const { return m_primitives; }
    };

    struct GeometryBundle
    {
        struct Chunk
        {
            std::shared_ptr<const MeshSurfaceInput> input;
        };
        std::vector<Chunk> m_chunks;
        std::shared_ptr<const MeshSurfaceInput> m_source;
        const auto& Chunks() const { return m_chunks; }
        const auto& Source() const { return m_source; }
    };

    enum class SceneCoverage
    {
        Opaque,
        Blended
    };
    struct EnhancedMaterialCoverage
    {
        static constexpr std::uint32_t DoubleSided = 4;
        std::uint32_t flags{};
        float cutoff{};
    };
    struct SealedProgram
    {
        bool surface{};
        std::uint32_t features{};
    };
    struct Generation
    {
        struct Cooked
        {
            struct Product
            {
                SealedProgram program;
            } product;
        } cooked;
    };
    struct Instance
    {
        std::shared_ptr<Generation> generation;
    };
    struct FrameCameraSnapshot
    {
        math::matrix4x4 projection;
        bool HasSameTemporalHistory(const FrameCameraSnapshot&) const { return true; }
    };
    struct SceneViewInput
    {
        struct Draw
        {
            std::shared_ptr<const GeometryBundle> geometry, previousGeometry;
            std::shared_ptr<Instance> material;
            std::uint64_t geometryKey{}, temporalObjectId{}, temporalIncarnation{}, temporalInstanceId{},
                materialSlot{};
            SceneCoverage queue{};
            float viewDepth{};
            bool temporalHistoryValid{};
            EnhancedMaterialCoverage coverage;
        };
        struct CostData
        {
            std::size_t chunks{};
        } m_cost;
        struct ViewData
        {
            std::uint64_t sceneEpoch{1}, viewId{1}, frameId{3}, historyRevision{7};
            std::uint32_t width{64}, height{64};
            FrameCameraSnapshot camera;
            struct Temporal
            {
                struct Camera
                {
                    bool valid{};
                    std::array<float, 16> projectionMatrix{};
                } camera;
                std::uint64_t historyRevision{9}, realFrameId{3};
                bool reset{};
            } temporalFrame;
        } m_view;
        math::matrix4x4 m_projection;
        std::vector<Draw> m_draws;
        const auto& Draws() const { return m_draws; }
        const auto& Cost() const { return m_cost; }
        const auto& View() const { return m_view; }
        const auto& ViewProjection() const { return m_projection; }
        float GeometryMaxPixelError() const { return 1.f; }
    };

#include "SceneForwardDraw.inl"
#include "SceneConstants.inl"
    static_assert(sizeof(SceneConstants) == 4800);
    static_assert(offsetof(SceneConstants, lights) == 368);

#include "SceneCpuPreparation.inl"

    math::matrix4x4 matrix(std::uint32_t seed)
    {
        math::matrix4x4 result;
        for (unsigned row = 0; row < 4; ++row)
        {
            for (unsigned column = 0; column < 4; ++column)
            {
                result.m[row][column] = float(seed * 17 + row * 4 + column + 1) / 128.f;
            }
        }
        return result;
    }

    std::shared_ptr<const MeshSurfaceInput> make_mesh(std::uint32_t seed, const std::shared_ptr<MetadataGate>& gate)
    {
        auto mesh = std::make_shared<MeshSurfaceInput>();
        mesh->m_world = matrix(seed);
        mesh->m_hierarchy.groupCount = seed % 2;
        mesh->m_geometry.sourceLodIndex = seed % 5;
        mesh->m_primitives = 30 + seed % 91;
        mesh->m_vertices = 45 + seed % 187;
        mesh->m_batch = std::make_shared<MeshSurfaceBatch>(MeshSurfaceBatch{1 + seed % 13, seed + 1});
        mesh->m_gate = gate;
        return mesh;
    }

    std::shared_ptr<SceneViewInput> make_input(std::size_t chunks, std::size_t draws, bool mixed,
                                               const std::shared_ptr<MetadataGate>& gate = {})
    {
        require(!chunks || (draws && draws <= chunks), "invalid probe input shape");
        auto input = std::make_shared<SceneViewInput>();
        input->m_cost.chunks = chunks;
        input->m_projection = matrix(3000);
        input->m_draws.reserve(draws);
        std::uint32_t seed = 1;
        for (std::size_t index = 0; index < draws; ++index)
        {
            const auto count = chunks / draws + (index < chunks % draws ? 1 : 0);
            auto geometry = std::make_shared<GeometryBundle>();
            geometry->m_chunks.reserve(count);
            for (std::size_t chunk = 0; chunk < count; ++chunk)
            {
                geometry->m_chunks.push_back({make_mesh(seed++, gate)});
            }
            geometry->m_source = geometry->m_chunks.front().input;
            SceneViewInput::Draw draw;
            draw.geometry = geometry;
            if (index % 3 == 1)
            {
                draw.previousGeometry = geometry;
            }
            else if (index % 3 == 2)
            {
                auto previous = std::make_shared<GeometryBundle>();
                previous->m_chunks.reserve(count);
                for (std::size_t chunk = 0; chunk + 1 < count; ++chunk)
                {
                    previous->m_chunks.push_back({make_mesh(seed++, gate)});
                }
                previous->m_source = geometry->m_source;
                draw.previousGeometry = previous;
            }
            draw.material = std::make_shared<Instance>();
            draw.material->generation = std::make_shared<Generation>();
            auto& program = draw.material->generation->cooked.product.program;
            program.surface = !mixed || index % 7 != 6;
            program.features = mixed && index % 4 == 2 ? 0x0800u : 0u;
            if (mixed && index % 7 == 6)
            {
                program.features |= 0x1000u;
            }
            draw.queue = mixed && index % 4 == 1 ? SceneCoverage::Blended : SceneCoverage::Opaque;
            draw.geometryKey = index % 7;
            draw.temporalObjectId = 100 + index;
            draw.temporalIncarnation = 200 + index;
            draw.temporalInstanceId = 300 + index;
            draw.materialSlot = 400 + index;
            draw.temporalHistoryValid = index % 2 != 0;
            draw.coverage = {static_cast<std::uint32_t>(index % 8), float(index % 5) / 5.f};
            draw.viewDepth = index % 13 == 0 ? -0.f : float(static_cast<int>(index % 9) - 4);
            input->m_draws.push_back(std::move(draw));
        }
        return input;
    }

    template<class Left, class Right>
    void compare_chunks(const Left& left, const Right& right)
    {
        require(left.size() == right.size(), "chunk count changed");
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto& a = left[index];
            const auto& b = right[index];
            require(a.input == b.input && a.chunk == b.chunk && a.owner == b.owner, "chunk prefix/owner changed");
            require(std::memcmp(&a.identity, &b.identity, sizeof(a.identity)) == 0, "geometry identity changed");
            require(std::memcmp(&a.world, &b.world, sizeof(a.world)) == 0, "current world changed");
            require(std::memcmp(&a.previousWorld, &b.previousWorld, sizeof(a.previousWorld)) == 0,
                    "previous world fallback changed");
            require(std::memcmp(a.worldColumns.data(), b.worldColumns.data(), sizeof(a.worldColumns)) == 0,
                    "current columns changed");
            require(std::memcmp(a.previousWorldColumns.data(), b.previousWorldColumns.data(),
                                sizeof(a.previousWorldColumns)) == 0,
                    "previous columns changed");
        }
    }

    void compare_blended(const std::vector<ForwardDraw>& left, const std::vector<ForwardDraw>& right)
    {
        require(left.size() == right.size(), "blended draw count changed");
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            require(left[index].index == right[index].index && left[index].geometryKey == right[index].geometryKey &&
                        std::bit_cast<std::uint32_t>(left[index].depth) ==
                            std::bit_cast<std::uint32_t>(right[index].depth),
                    "blended total order changed");
        }
    }

    struct DurableOutputs
    {
        std::vector<std::pair<std::size_t, std::size_t>> m_ranges;
        std::vector<ForwardDraw> m_blended;
    };

    void verify_metadata(job_scheduler& scheduler)
    {
        optimized::SceneHost::PreparationScratch scratch;
        std::string error;
        for (const auto chunks : {std::size_t{0}, std::size_t{1}, std::size_t{127}, std::size_t{128}, std::size_t{129},
                                  std::size_t{512}, std::size_t{4096}})
        {
            for (const bool mixed : {false, true})
            {
                const own::shared_owner<const SceneViewInput> input =
                    make_input(chunks, (std::min)(chunks, std::size_t{73}), mixed);
                baseline::SceneCpuPreparation baseline;
                baseline.Start(scheduler, input);
                require(baseline.Join(error), "baseline metadata failed");
                DurableOutputs candidate;
                optimized::SceneCpuPreparation prepared(candidate.m_ranges, scratch.m_chunks, candidate.m_blended);
                prepared.Start(scheduler, input);
                require(prepared.Join(error) && prepared.Join(error), "optimized metadata failed");
                require(candidate.m_ranges == baseline.ranges, "durable input prefix ranges changed");
                compare_chunks(scratch.m_chunks, baseline.chunks);
                compare_blended(candidate.m_blended, baseline.blended);
                if (!mixed)
                {
                    require(candidate.m_blended.capacity() == 0, "opaque view allocated a blended result");
                }
            }
        }
        const auto* retained = scratch.m_chunks.data();
        const auto capacity = scratch.m_chunks.capacity();
        const own::shared_owner<const SceneViewInput> small = make_input(17, 7, true);
        DurableOutputs earlier;
        {
            optimized::SceneCpuPreparation prepared(earlier.m_ranges, scratch.m_chunks, earlier.m_blended);
            prepared.Start(scheduler, small);
            require(prepared.Join(error), "small reused metadata failed");
        }
        const auto ranges = earlier.m_ranges;
        const auto blended = earlier.m_blended;
        const auto* earlierRanges = earlier.m_ranges.data();
        const auto* earlierBlended = earlier.m_blended.data();
        DurableOutputs next;
        const own::shared_owner<const SceneViewInput> large = make_input(4096, 1024, true);
        {
            optimized::SceneCpuPreparation prepared(next.m_ranges, scratch.m_chunks, next.m_blended);
            prepared.Start(scheduler, large);
            require(prepared.Join(error), "large reused metadata failed");
        }
        require(scratch.m_chunks.data() == retained && scratch.m_chunks.capacity() == capacity,
                "high-water chunk capacity was not retained");
        require(earlier.m_ranges.data() == earlierRanges && earlier.m_blended.data() == earlierBlended &&
                    earlier.m_ranges == ranges,
                "retained frame output storage was reused");
        compare_blended(earlier.m_blended, blended);

        auto gate = std::make_shared<MetadataGate>();
        auto failing = make_input(512, 128, true, gate);
        failing->m_draws[1].viewDepth = std::numeric_limits<float>::quiet_NaN();
        {
            optimized::SceneCpuPreparation prepared(next.m_ranges, scratch.m_chunks, next.m_blended);
            bool failed{};
            try
            {
                prepared.Start(scheduler, failing);
                failed = !prepared.Join(error) && error.find("non-finite") != std::string::npos;
            }
            catch (const std::runtime_error& failure)
            {
                failed = std::string_view(failure.what()).find("non-finite") != std::string_view::npos;
            }
            require(failed, "worker/inline sort failure was not reported");
        }
        require(gate->m_reads.load() == 512 * 8, "sort failure escaped before metadata tasks drained");
        auto mismatched = make_input(8, 4, false);
        ++mismatched->m_cost.chunks;
        bool rejected{};
        try
        {
            optimized::SceneCpuPreparation prepared(next.m_ranges, scratch.m_chunks, next.m_blended);
            prepared.Start(scheduler, mismatched);
        }
        catch (const std::runtime_error& failure)
        {
            rejected = std::string_view(failure.what()).find("sealed chunk cost") != std::string_view::npos;
        }
        require(rejected, "mismatched immutable chunk cost was accepted");
    }

    void verify_metadata_drain(job_scheduler& scheduler)
    {
        auto gate = std::make_shared<MetadataGate>();
        gate->m_release.store(false);
        own::shared_owner<const SceneViewInput> input = make_input(512, 128, true, gate);
        std::weak_ptr<const SceneViewInput> lifetime = input;
        optimized::SceneHost::PreparationScratch scratch;
        DurableOutputs output;
        std::atomic<bool> ownerFailing{};
        std::thread release(
            [&]
            {
                await_ready([&] { return ownerFailing.load(std::memory_order_acquire); });
                require(!lifetime.expired(), "accepted metadata lost its sealed input");
                gate->m_release.store(true, std::memory_order_release);
            });
        bool caught{};
        try
        {
            optimized::SceneCpuPreparation prepared(output.m_ranges, scratch.m_chunks, output.m_blended);
            prepared.Start(scheduler, input);
            await_ready([&] { return gate->m_entered.load(std::memory_order_acquire); });
            input.reset();
            ownerFailing.store(true, std::memory_order_release);
            throw std::runtime_error("owner preparation failure");
        }
        catch (const std::runtime_error& failure)
        {
            caught = std::string_view(failure.what()) == "owner preparation failure";
        }
        release.join();
        require(caught && gate->m_reads.load() == 512 * 8, "owner exception escaped before metadata drain");
        require(lifetime.expired(), "completed metadata retained its sealed input");
        std::atomic<bool> nestedFinished{};
        auto nested = scheduler.submit_indexed(
            1,
            [&](std::size_t)
            {
                const own::shared_owner<const SceneViewInput> workerInput = make_input(512, 128, true);
                optimized::SceneCpuPreparation prepared(output.m_ranges, scratch.m_chunks, output.m_blended);
                prepared.Start(scheduler, workerInput);
                std::string error;
                require(prepared.Join(error), "worker inline metadata failed");
                nestedFinished.store(true, std::memory_order_release);
            });
        nested.wait();
        require(nestedFinished.load(std::memory_order_acquire), "nested metadata did not execute inline");
    }
} // namespace

namespace
{
    enum class PipelineFailure
    {
        None,
        OwnerUpload,
        GeometryPublication,
        FirstVisibility,
        RecoveryVisibility,
        BlendedVisibility,
        Surface
    };

    struct PipelineGate
    {
        bool m_overlap{};
        bool m_cameraExpected{true};
        PipelineFailure m_failure{};
        std::thread::id m_owner = std::this_thread::get_id();
        std::atomic<bool> m_cameraStarted{}, m_geometryPublished{}, m_ownerFailed{}, m_visibilityFailed{};
        std::atomic<bool> m_shadowStarted{}, m_shadowCompleted{}, m_releaseShadow{}, m_surfaceStarted{};
        std::atomic<bool> m_ownerObserved{}, m_hybridReady{};
        std::atomic<unsigned> m_visibilityActive{}, m_visibilityFinished{};
        void (*m_ownerCheck)(void*){};
        void* m_ownerContext{};
        void reject_gate(const char* message)
        {
            m_ownerFailed.store(true, std::memory_order_release);
            m_visibilityFailed.store(true, std::memory_order_release);
            m_releaseShadow.store(true, std::memory_order_release);
            throw std::runtime_error(message);
        }
        template<class Predicate>
        void await_pipeline_gate(Predicate&& ready)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!ready())
            {
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    reject_gate("fixture pipeline gate did not release its dependency");
                }
                std::this_thread::yield();
            }
        }
        void OwnerMetadata()
        {
            require(std::this_thread::get_id() == m_owner, "owner metadata moved to a worker");
            if (!m_overlap || m_ownerObserved.exchange(true))
            {
                return;
            }
            await_pipeline_gate([&] { return m_cameraStarted.load() || m_visibilityFailed.load(); });
            await_pipeline_gate([&] { return m_shadowStarted.load(); });
            require(!m_geometryPublished.load(), "camera launch missed the owner metadata window");
            if (!m_visibilityFailed.load())
            {
                require(m_visibilityActive.load() != 0, "camera did not overlap owner metadata");
            }
            if (m_ownerCheck)
            {
                m_ownerCheck(m_ownerContext);
            }
        }
    };

    struct EnhancedFrameContext
    {
        PipelineGate* m_gate{};
    };
    struct RHIBufferSlice
    {
        std::uint32_t id{};
        bool IsValid() const { return id != 0; }
        bool operator==(const RHIBufferSlice&) const = default;
    };
    struct RenderBindings
    {
        std::uint32_t m_id{};
    };
    struct Program
    {
        bool hasSurface{}, hasSpecial{}, hasTransmission{}, meshEnabled{true};
    };

    struct GpuGeometryOcclusion
    {
        struct Pyramid
        {
            struct Source
            {
                std::uint64_t frameId{};
                std::uint32_t width{}, height{};
            } source;
            std::uint64_t completion{}, revision{};
        };
        struct History : Pyramid
        {
        };
        static std::shared_ptr<const Pyramid> ReadHistory(const EnhancedFrameContext&, const math::matrix4x4&,
                                                          const std::shared_ptr<History>& history)
        {
            return history;
        }
        bool PrepareHistory(const EnhancedFrameContext& context, const math::matrix4x4&, std::uint64_t, std::uint64_t,
                            std::shared_ptr<History>& output, std::string&)
        {
            require(std::this_thread::get_id() == context.m_gate->m_owner, "camera history builder changed affinity");
            output = std::make_shared<History>();
            return true;
        }
        bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4&,
                     std::shared_ptr<const Pyramid>& output, std::string&)
        {
            require(std::this_thread::get_id() == context.m_gate->m_owner, "camera pyramid builder changed affinity");
            output = std::make_shared<Pyramid>();
            return true;
        }
    };

    struct GpuGeometryVisibility
    {
        struct Frame
        {
            std::vector<std::uint32_t> m_geometry;
            std::vector<std::uint32_t> m_offsets;
            bool m_recovery{}, m_blended{};
            std::uint32_t MeshletOutputOffset(std::uint32_t bin) const { return m_offsets.at(bin); }
        };
        bool PrepareMeshlets(const EnhancedFrameContext& context, const math::matrix4x4&,
                             const std::vector<std::shared_ptr<const MeshSurfaceBatch>>& geometry,
                             std::shared_ptr<const Frame>& output, std::string& error, bool recovery,
                             std::shared_ptr<const GpuGeometryOcclusion::Pyramid>,
                             std::shared_ptr<const Frame> previous, bool blended, float)
        {
            auto& gate = *context.m_gate;
            gate.m_visibilityActive.fetch_add(1);
            struct Completion
            {
                PipelineGate& m_gate;
                ~Completion()
                {
                    m_gate.m_visibilityFinished.fetch_add(1);
                    m_gate.m_visibilityActive.fetch_sub(1);
                }
            } completion{gate};
            const auto count = geometry.size();
            require(count != 0, "empty meshlet input was admitted");
            if (recovery)
            {
                require(previous && !previous->m_recovery && !previous->m_blended &&
                            previous->m_geometry.size() == count,
                        "recovery lost the completed first list dependency");
            }
            else if (!blended || !gate.m_cameraExpected)
            {
                gate.m_cameraStarted.store(true, std::memory_order_release);
            }
            const bool fail = (gate.m_failure == PipelineFailure::FirstVisibility && !blended && !recovery) ||
                              (gate.m_failure == PipelineFailure::RecoveryVisibility && recovery) ||
                              (gate.m_failure == PipelineFailure::BlendedVisibility && blended);
            if (fail)
            {
                gate.m_visibilityFailed.store(true, std::memory_order_release);
                return Fail(error, "probe visibility failure");
            }
            if (gate.m_overlap)
            {
                if (std::this_thread::get_id() == gate.m_owner && !gate.m_geometryPublished.load() &&
                    !gate.m_ownerFailed.load() && !gate.m_visibilityFailed.load())
                {
                    gate.reject_gate("fixture camera gate was assisted by its dependent owner");
                }
                gate.await_pipeline_gate(
                    [&]
                    {
                        return gate.m_geometryPublished.load(std::memory_order_acquire) ||
                               gate.m_ownerFailed.load(std::memory_order_acquire) ||
                               gate.m_visibilityFailed.load(std::memory_order_acquire);
                    });
            }
            require(geometry.size() == count, "borrowed camera storage cleared before accepted work drained");
            auto candidate = std::make_shared<Frame>();
            candidate->m_geometry.reserve(count);
            candidate->m_offsets.reserve(count);
            candidate->m_recovery = recovery;
            candidate->m_blended = blended;
            std::uint32_t next{};
            for (const auto& batch : geometry)
            {
                require(bool(batch), "borrowed geometry owner was released before drain");
                candidate->m_geometry.push_back(batch->m_id);
                candidate->m_offsets.push_back(next);
                next += batch->m_meshlets;
            }
            if (previous)
            {
                require(candidate->m_geometry == previous->m_geometry && candidate->m_offsets == previous->m_offsets,
                        "recovery reordered geometry input bins");
            }
            output = std::move(candidate);
            return true;
        }
    };

    struct FakeDevice
    {
        struct MeshCapabilities
        {
            bool SupportsDispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) const
            {
                return x && x <= 65535 && y && y <= 65535 && z == 1;
            }
        };
        PipelineGate* m_gate{};
        std::vector<std::array<std::byte, 4800>> m_uploads;
        MeshCapabilities GetMeshShaderCapabilities() const { return {}; }
        RHIBufferSlice UploadConstants(const void* bytes, std::size_t count)
        {
            m_gate->OwnerMetadata();
            require(count == 4800, "unexpected constant payload size");
            if (m_gate->m_failure == PipelineFailure::OwnerUpload)
            {
                m_gate->m_ownerFailed.store(true, std::memory_order_release);
                return {};
            }
            m_uploads.emplace_back();
            std::memcpy(m_uploads.back().data(), bytes, count);
            return {static_cast<std::uint32_t>(m_uploads.size())};
        }
    };

    struct SceneFrameGeometry
    {
#include "SceneGeometryRecord.inl"
        std::vector<Record> records;
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> geometry;
        std::uint32_t nextPrimitiveAddress{1};
    };
    static_assert(sizeof(SceneFrameGeometry::Record) == 128);

    struct SceneGpuData
    {
#include "SceneGpuRecord.inl"
        PipelineGate* m_gate{};
        std::vector<Record> records;
        RHIBufferSlice viewConstants{9000};
        std::array<RHIBufferSlice, 3> shadowConstants{{{9100}, {9101}, {9102}}};
        bool AssignGeometry(std::uint32_t record, const std::shared_ptr<const MeshSurfaceBatch>& current,
                            const std::shared_ptr<const MeshSurfaceBatch>& previous, std::string&)
        {
            m_gate->OwnerMetadata();
            records.at(record).geometry = {current->m_id, previous->m_id, current->m_id + 20000, current->m_meshlets};
            return true;
        }
    };
    static_assert(sizeof(SceneGpuData::Record) == 224);

    struct HybridVisibilityPipelines
    {
        bool Prepare(const EnhancedFrameContext& context, std::string&)
        {
            require(std::this_thread::get_id() == context.m_gate->m_owner, "hybrid preflight changed affinity");
            context.m_gate->m_hybridReady.store(true);
            return true;
        }
    };
    struct Frame;
    struct SurfaceStage
    {
        struct Surface
        {
        };
        std::shared_ptr<const Surface> occluderVisibility;
        bool Prepare(const Frame& frame, const EnhancedFrameContext& context,
                     const std::shared_ptr<SceneFrameGeometry>& geometry,
                     const std::shared_ptr<const GpuGeometryVisibility::Frame>& list, HybridVisibilityPipelines&,
                     std::string& error, const Surface* = nullptr);
    };
    struct GeometryStage
    {
        struct Key
        {
            std::uint64_t scene{}, view{}, slot{};
        };
        PipelineGate* m_gate{};
        std::shared_ptr<SceneFrameGeometry> geometryTable;
        bool Prepare(std::shared_ptr<SceneFrameGeometry> geometry, std::string& error, int*, Key)
        {
            require(std::this_thread::get_id() == m_gate->m_owner, "geometry publication changed affinity");
            if (m_gate->m_failure == PipelineFailure::GeometryPublication)
            {
                m_gate->m_ownerFailed.store(true, std::memory_order_release);
                return Fail(error, "probe geometry publication failure");
            }
            geometryTable = std::move(geometry);
            m_gate->m_geometryPublished.store(true, std::memory_order_release);
            return true;
        }
    };
    struct Frame
    {
#include "SceneFrameDraw.inl"
        FakeDevice* device{};
        own::shared_owner<const SceneViewInput> input;
        std::vector<Draw> draws, shadowDraws;
        std::vector<std::pair<std::size_t, std::size_t>> inputRanges;
        std::vector<ForwardDraw> blendedOrder;
        struct FirstCull
        {
            std::shared_ptr<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
        } firstCullStage;
        struct FinalCull
        {
            std::shared_ptr<const GpuGeometryVisibility::Frame> cameraMeshlets, blendedMeshlets;
            std::shared_ptr<const GpuGeometryOcclusion::Pyramid> currentPyramid;
        } finalCullStage;
        struct History
        {
            std::shared_ptr<GpuGeometryOcclusion::History> nextCameraHistory;
        } historyStage;
        GeometryStage geometryStage;
        SurfaceStage occluderStage, visibilityStage;
#include "SceneUsesMeshlets.inl"
    };

    bool SurfaceStage::Prepare(const Frame& frame, const EnhancedFrameContext& context,
                               const std::shared_ptr<SceneFrameGeometry>& geometry,
                               const std::shared_ptr<const GpuGeometryVisibility::Frame>& list,
                               HybridVisibilityPipelines&, std::string& error, const Surface*)
    {
        auto& gate = *context.m_gate;
        require(std::this_thread::get_id() == gate.m_owner, "dependent surface setup left the owner");
        require(gate.m_geometryPublished.load() && geometry && gate.m_visibilityActive.load() == 0,
                "surface setup ran before geometry publication or visibility join");
        for (const auto& draw : frame.draws)
        {
            require(draw.bindings && draw.geometryRecord && draw.constants.IsValid(), "surface saw an incomplete Draw");
            if (draw.program->hasSurface && !draw.program->hasTransmission && !draw.blended)
            {
                require(list && gate.m_hybridReady.load() && draw.cameraMeshletBin < list->m_geometry.size() &&
                            list->m_geometry[draw.cameraMeshletBin] == draw.geometry->m_id,
                        "surface lost its exact camera bin/geometry relation");
            }
        }
        if (!gate.m_surfaceStarted.exchange(true) && gate.m_overlap)
        {
            require(gate.m_shadowStarted.load() && !gate.m_shadowCompleted.load(),
                    "surface setup unnecessarily waited for shadow setup");
        }
        gate.m_releaseShadow.store(true, std::memory_order_release);
        if (gate.m_failure == PipelineFailure::Surface)
        {
            return Fail(error, "probe surface failure");
        }
        occluderVisibility = std::make_shared<Surface>();
        return true;
    }

    struct DrawPreparation
    {
        own::shared_owner<const RenderBindings> bindings;
        RHIBufferSlice visibleOwner;
        std::pair<std::uint32_t, std::uint32_t> materialOffsets;
        bool shadowEnabled{};
        std::array<RHIBufferSlice, 3> shadowConstants;
    };
    struct CameraHistory
    {
        std::shared_ptr<GpuGeometryOcclusion::History> pyramid;
        FrameCameraSnapshot camera;
        std::uint64_t temporalRevision{};
    };

    struct PipelineState
    {
        using PreparationScratch = optimized::SceneHost::PreparationScratch;
        job_scheduler& m_scheduler;
        PipelineGate m_gate;
        FakeDevice m_device{&m_gate};
        EnhancedFrameContext m_context{&m_gate};
        GpuGeometryVisibility m_visibility;
        GpuGeometryOcclusion m_cameraOcclusion;
        HybridVisibilityPipelines m_hybridPipelines;
        std::atomic_flag m_preparing{};
        std::unique_ptr<PreparationScratch> m_scratch = std::make_unique<PreparationScratch>();
        std::shared_ptr<Frame> m_candidate = std::make_shared<Frame>();
        std::shared_ptr<SceneFrameGeometry> m_geometry = std::make_shared<SceneFrameGeometry>();
        std::shared_ptr<SceneGpuData> m_sceneData = std::make_shared<SceneGpuData>();
        std::vector<own::shared_owner<const Program>> m_programs;
        std::vector<DrawPreparation> m_draws;
        std::vector<std::uint32_t> m_shadowChunkIndices;
        std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<CameraHistory>> m_cameraHistories;
        std::mutex m_recordingMutex;
        std::uint64_t m_completed{};
        SceneConstants m_viewConstants{};
        struct Temporal
        {
            bool reset{};
        } m_temporal;
        struct Shadow
        {
            bool enabled{true};
        } m_shadow;
        int m_residentBuffers{};
        std::string m_error;
        unsigned m_mutations{};

        PipelineState(job_scheduler& scheduler, own::shared_owner<const SceneViewInput> input, bool reset)
            : m_scheduler(scheduler)
        {
            m_candidate->input = std::move(input);
            m_candidate->device = &m_device;
            m_candidate->geometryStage.m_gate = &m_gate;
            m_sceneData->m_gate = &m_gate;
            m_temporal.reset = reset;
            m_viewConstants.viewProjection = matrix(101);
            m_viewConstants.eye = {1.f, 2.f, 3.f, 4.f};
            m_viewConstants.width = 64;
            m_viewConstants.height = 64;
            m_viewConstants.environment = 17;
            m_viewConstants.hasShadow = 1;
            m_viewConstants.shadowBlend = .7f;
            m_viewConstants.padding[1] = 49;
            m_viewConstants.temporalCurrentProjection = matrix(102);
            m_viewConstants.temporalPreviousProjection = matrix(103);
            for (unsigned index = 0; index < 3; ++index)
            {
                m_viewConstants.shadowProjection[index] = matrix(104 + index);
            }
            m_viewConstants.shadowSplits = {.1f, .3f, .8f, 1.f};
            m_viewConstants.shadowBias = {.2f, .4f, .6f, .8f};
            m_viewConstants.cameraForward = {0.f, 0.f, 1.f, 0.f};
            for (unsigned index = 0; index < 64; ++index)
            {
                auto& light = m_viewConstants.lights[index];
                light.position = {float(index), 1.f, 2.f, 3.f};
                light.direction = {float(index + 1), 2.f, 3.f, 4.f};
                light.color = {float(index + 2), 3.f, 4.f, 5.f};
                light.attenuation = {float(index + 3), 4.f, 5.f, 6.f};
            }
        }
    };

    bool enter_only(PipelineState& state, std::string& error)
    {
        using PreparationScratch = PipelineState::PreparationScratch;
        auto& m_preparing = state.m_preparing;
        auto& m_preparationScratch = state.m_scratch;
#include "ScenePrepareGuard.inl"
        ++state.m_mutations;
        return true;
    }

    void check_reentry(void* context)
    {
        auto& state = *static_cast<PipelineState*>(context);
        const auto cameraCount = state.m_scratch->m_cameraGeometry.size();
        const auto mutations = state.m_mutations;
        std::string sameError, concurrentError;
        require(!enter_only(state, sameError), "same-thread host reentry was accepted");
        bool concurrentAccepted{};
        std::thread concurrent([&] { concurrentAccepted = enter_only(state, concurrentError); });
        concurrent.join();
        require(!concurrentAccepted && state.m_mutations == mutations && cameraCount != 0 &&
                    state.m_scratch->m_cameraGeometry.size() == cameraCount && state.m_preparing.test(),
                "rejected host entry mutated state or released active scratch");
        require(sameError.find("reenter") != std::string::npos && concurrentError.find("reenter") != std::string::npos,
                "host reentry rejection lost its error");
    }

    bool initialize_geometry(PipelineState& state)
    {
        auto candidate = state.m_candidate;
        auto& scheduler_ = state.m_scheduler;
        auto& error = state.m_error;
        const auto preparedChunks = candidate->input->Cost().chunks;
        optimized::SceneCpuPreparation cpu(candidate->inputRanges, state.m_scratch->m_chunks, candidate->blendedOrder);
        cpu.Start(scheduler_, candidate->input);
        if (!cpu.Join(error))
        {
            return false;
        }
        candidate->draws.resize(preparedChunks);
        candidate->shadowDraws.reserve(preparedChunks);
        state.m_geometry->records.resize(1 + preparedChunks);
        state.m_geometry->geometry.resize(1 + preparedChunks);
        state.m_sceneData->records.resize(1 + preparedChunks);
        state.m_programs.reserve(candidate->input->Draws().size());
        state.m_draws.reserve(candidate->input->Draws().size());
        state.m_shadowChunkIndices.resize(preparedChunks);
        state.m_device.m_uploads.reserve(preparedChunks * 2);
        for (std::size_t index = 0; index < candidate->input->Draws().size(); ++index)
        {
            const auto& draw = candidate->input->Draws()[index];
            const auto& sealed = draw.material->generation->cooked.product.program;
            auto program = std::make_shared<Program>();
            program->hasSurface = sealed.surface;
            program->hasSpecial = (sealed.features & 0x1800u) != 0;
            program->hasTransmission = (sealed.features & 0x0800u) != 0;
            state.m_programs.push_back(program);
            DrawPreparation prepared;
            prepared.bindings = std::make_shared<RenderBindings>(RenderBindings{static_cast<std::uint32_t>(index)});
            prepared.visibleOwner = {static_cast<std::uint32_t>(index + 1000)};
            prepared.materialOffsets = {static_cast<std::uint32_t>(index * 512),
                                        static_cast<std::uint32_t>(index * 512 + 16)};
            prepared.shadowEnabled = program->hasSurface && draw.queue != SceneCoverage::Blended;
            prepared.shadowConstants = state.m_sceneData->shadowConstants;
            state.m_draws.push_back(std::move(prepared));
        }
        const auto& drawPrograms = state.m_programs;
        const auto& shadow = state.m_shadow;
        auto& shadowChunkIndices = state.m_shadowChunkIndices;
        const auto prepareGeometry =
            [](const std::shared_ptr<const MeshSurfaceInput>& source, std::shared_ptr<const MeshSurfaceBatch>& output)
        {
            output = source->m_batch;
            return true;
        };
#include "SceneInitialGeometry.inl"
        return true;
    }

    bool execute_baseline_draws(PipelineState& state)
    {
        require(initialize_geometry(state), "baseline geometry setup failed");
        auto candidate = state.m_candidate;
        auto geometryTable = state.m_geometry;
        auto sceneData = state.m_sceneData;
        auto* device_ = &state.m_device;
        const auto& drawPrograms = state.m_programs;
        const auto& preparedDraws = state.m_draws;
        const auto& temporal = state.m_temporal;
        const auto& viewConstants = state.m_viewConstants;
        constexpr std::size_t lightCount = 65, storedLights = 64;
        auto& error = state.m_error;
        const auto nextPreparationPhase = [](ce::marker_id) {};
        struct CpuView
        {
            const std::vector<std::pair<std::size_t, std::size_t>>& ranges;
            const std::vector<optimized::SceneCpuPreparation::Chunk>& chunks;
        } cpuPreparation{candidate->inputRanges, state.m_scratch->m_chunks};
#include "SceneBaselineDraws.inl"
        return true;
    }

    bool execute_pipeline(PipelineState& state, bool parallel)
    {
        using PreparationScratch = PipelineState::PreparationScratch;
        auto& m_preparing = state.m_preparing;
        auto& m_preparationScratch = state.m_scratch;
        auto& error = state.m_error;
#include "ScenePrepareGuard.inl"
        ++state.m_mutations;
        if (!initialize_geometry(state))
        {
            return false;
        }
        auto candidate = state.m_candidate;
        auto geometryTable = state.m_geometry;
        auto sceneData = state.m_sceneData;
        auto* device_ = &state.m_device;
        auto& scheduler_ = state.m_scheduler;
        const auto& context = state.m_context;
        const auto& drawPrograms = state.m_programs;
        const auto& preparedDraws = state.m_draws;
        const auto& temporal = state.m_temporal;
        const auto& viewConstants = state.m_viewConstants;
        constexpr std::size_t lightCount = 65, storedLights = 64;
        const auto preparedChunks = candidate->input->Cost().chunks;
        const bool parallelPreparation = parallel;
        const auto nextPreparationPhase = [](ce::marker_id) {};
        auto& recordingMutex_ = state.m_recordingMutex;
        auto& completed_ = state.m_completed;
        auto& cameraHistories_ = state.m_cameraHistories;
        auto& cameraOcclusion_ = state.m_cameraOcclusion;
        auto& visibility_ = state.m_visibility;
        auto& sceneResidentBuffers_ = state.m_residentBuffers;
        auto& hybridPipelines_ = state.m_hybridPipelines;
        ScenePreparationTasks drawPreparation(scheduler_, parallelPreparation);
        require(drawPreparation.Add(
                    ce::marker<"ProbeDrawBindings">(), [](std::string&) { return true; }, error),
                "draw binding probe submission failed");
        // 가짜 binding 작업을 먼저 완료해 owner의 assisting wait가 미래 owner 단계에 매인
        // 검증용 gate를 실행하지 않게 한다. 실제 camera 준비 본문은 그대로 검증한다.
        require(drawPreparation.Join(error), "draw binding fixture prerequisite failed");
        ScenePreparationTasks shadowSetupPreparation(scheduler_, parallelPreparation);
        require(shadowSetupPreparation.Add(
                    ce::marker<"ProbeShadowSetup">(),
                    [&](std::string&)
                    {
                        state.m_gate.m_shadowStarted.store(true, std::memory_order_release);
                        if (parallelPreparation)
                        {
                            if (std::this_thread::get_id() == state.m_gate.m_owner)
                            {
                                state.m_gate.reject_gate("fixture shadow gate was assisted by its dependent owner");
                            }
                            state.m_gate.await_pipeline_gate(
                                [&]
                                {
                                    return state.m_gate.m_releaseShadow.load(std::memory_order_acquire) ||
                                           state.m_gate.m_ownerFailed.load(std::memory_order_acquire) ||
                                           state.m_gate.m_visibilityFailed.load(std::memory_order_acquire);
                                });
                        }
                        std::uint32_t bin{};
                        for (auto& draw : candidate->shadowDraws)
                        {
                            draw.shadowMeshletBin = bin;
                            draw.staticShadow = bin++ % 2 == 0;
                        }
                        state.m_gate.m_shadowCompleted.store(true, std::memory_order_release);
                        return true;
                    },
                    error),
                "shadow setup probe submission failed");
        if (parallelPreparation)
        {
            state.m_gate.await_pipeline_gate([&]
                                             { return state.m_gate.m_shadowStarted.load(std::memory_order_acquire); });
        }
#include "SceneCameraPipeline.inl"
        return true;
    }
} // namespace

namespace
{
    void compare_draw_outputs(const PipelineState& left, const PipelineState& right)
    {
        require(left.m_device.m_uploads == right.m_device.m_uploads,
                "hoisted SceneConstants changed a byte of an uploaded 4800-byte payload");
        require(left.m_geometry->records.size() == right.m_geometry->records.size() &&
                    std::memcmp(left.m_geometry->records.data(), right.m_geometry->records.data(),
                                left.m_geometry->records.size() * sizeof(SceneFrameGeometry::Record)) == 0,
                "frame geometry records changed");
        require(left.m_sceneData->records.size() == right.m_sceneData->records.size() &&
                    std::memcmp(left.m_sceneData->records.data(), right.m_sceneData->records.data(),
                                left.m_sceneData->records.size() * sizeof(SceneGpuData::Record)) == 0,
                "GPU scene records changed");
        require(left.m_geometry->nextPrimitiveAddress == right.m_geometry->nextPrimitiveAddress,
                "primitive prefix address changed");
        require(left.m_candidate->draws.size() == right.m_candidate->draws.size(), "final Draw count changed");
        for (std::size_t index = 0; index < left.m_candidate->draws.size(); ++index)
        {
            const auto& a = left.m_candidate->draws[index];
            const auto& b = right.m_candidate->draws[index];
            require(a.geometry == b.geometry && a.previousGeometry == b.previousGeometry &&
                        a.inputIndex == b.inputIndex && a.cameraMeshletBin == b.cameraMeshletBin &&
                        a.geometryRecord == b.geometryRecord && a.blended == b.blended &&
                        a.doubleSided == b.doubleSided && a.shadowEnabled == b.shadowEnabled &&
                        a.constants == b.constants && a.referenceConstants == b.referenceConstants &&
                        a.visibleOwner == b.visibleOwner && a.shadowConstants == b.shadowConstants &&
                        a.bindings->m_id == b.bindings->m_id,
                    "Draw overwrite changed a retained field or lost its camera bin");
        }
        for (const auto& shadow : right.m_candidate->shadowDraws)
        {
            const auto& draw = right.m_candidate->draws.at(shadow.geometryRecord - 1);
            require(shadow.bindings == draw.bindings && shadow.constants == draw.constants &&
                        shadow.cameraMeshletBin == draw.cameraMeshletBin && shadow.shadowMeshletBin != UINT32_MAX &&
                        shadow.staticShadow == (shadow.shadowMeshletBin % 2 == 0),
                    "shadow material linking lost its prepared snapshot fields");
        }
    }

    void verify_pipeline(job_scheduler& scheduler)
    {
        for (const bool parallel : {false, true})
        {
            for (const bool reset : {false, true})
            {
                const own::shared_owner<const SceneViewInput> input = make_input(73, 19, true);
                PipelineState baseline(scheduler, input, reset);
                require(execute_baseline_draws(baseline), "baseline DrawsPrepare failed");
                PipelineState optimized(scheduler, input, reset);
                optimized.m_gate.m_overlap = parallel;
                optimized.m_gate.m_ownerCheck = check_reentry;
                optimized.m_gate.m_ownerContext = &optimized;
                require(execute_pipeline(optimized, parallel), "optimized camera/DrawsPrepare failed");
                compare_draw_outputs(baseline, optimized);
                require(optimized.m_gate.m_surfaceStarted.load() && optimized.m_gate.m_shadowCompleted.load() &&
                            optimized.m_gate.m_visibilityFinished.load() == 3 &&
                            optimized.m_gate.m_visibilityActive.load() == 0 && !optimized.m_preparing.test(),
                        "successful Prepare did not join its dependencies and release exclusivity");
                require(optimized.m_scratch->m_cameraGeometry.empty() &&
                            optimized.m_scratch->m_blendedGeometry.empty() &&
                            optimized.m_scratch->m_cameraGeometry.capacity() >= input->Cost().chunks &&
                            optimized.m_scratch->m_blendedGeometry.capacity() >= input->Cost().chunks,
                        "camera scratch failed to release owners while retaining capacity");
                std::string error;
                require(enter_only(optimized, error), "successful Prepare did not restore admission");
            }
        }

        for (const auto failure :
             {PipelineFailure::OwnerUpload, PipelineFailure::GeometryPublication, PipelineFailure::FirstVisibility,
              PipelineFailure::RecoveryVisibility, PipelineFailure::BlendedVisibility, PipelineFailure::Surface})
        {
            const own::shared_owner<const SceneViewInput> input = make_input(41, 13, true);
            PipelineState state(scheduler, input, false);
            state.m_gate.m_overlap = true;
            state.m_gate.m_failure = failure;
            require(!execute_pipeline(state, true) && !state.m_error.empty(), "injected Prepare failure was accepted");
            require(state.m_gate.m_visibilityActive.load() == 0 && state.m_gate.m_shadowCompleted.load() &&
                        !state.m_preparing.test() && state.m_scratch->m_cameraGeometry.empty() &&
                        state.m_scratch->m_blendedGeometry.empty(),
                    "failed Prepare released scratch or exclusivity before accepted work drained");
            std::string error;
            require(enter_only(state, error), "failed Prepare did not restore admission");
        }

        auto blended = make_input(19, 7, true);
        for (auto& draw : blended->m_draws)
        {
            draw.queue = SceneCoverage::Blended;
            auto& program = draw.material->generation->cooked.product.program;
            program.surface = true;
            program.features = 0;
        }
        PipelineState allBlended(scheduler, blended, false);
        allBlended.m_gate.m_cameraExpected = false;
        allBlended.m_gate.m_overlap = true;
        require(execute_pipeline(allBlended, true) && !allBlended.m_candidate->firstCullStage.cameraDepthMeshlets &&
                    allBlended.m_candidate->finalCullStage.blendedMeshlets &&
                    allBlended.m_gate.m_visibilityFinished.load() == 1,
                "empty opaque camera path changed blended-only preparation");
        std::cout << "PASS pipeline: exact uploaded-byte/GPU-record parity, camera-bin retention, owner overlap, "
                     "first/recovery dependency, surface/shadow overlap, reentry rejection, failure drains\n";
    }

    template<class T>
    void escape(const T* data)
    {
        asm volatile("" : : "g"(data) : "memory");
    }

    void run_metadata(job_scheduler& scheduler, const own::shared_owner<const SceneViewInput>& input,
                      optimized::SceneHost::PreparationScratch& scratch, bool optimized)
    {
        std::string error;
        if (optimized)
        {
            DurableOutputs candidate;
            optimized::SceneCpuPreparation prepared(candidate.m_ranges, scratch.m_chunks, candidate.m_blended);
            prepared.Start(scheduler, input);
            require(prepared.Join(error), "benchmark optimized metadata failed");
            escape(candidate.m_ranges.data());
            escape(candidate.m_blended.data());
            escape(scratch.m_chunks.data());
        }
        else
        {
            baseline::SceneCpuPreparation prepared;
            prepared.Start(scheduler, input);
            require(prepared.Join(error), "benchmark baseline metadata failed");
            escape(prepared.ranges.data());
            escape(prepared.blended.data());
            escape(prepared.chunks.data());
        }
    }

    struct Timing
    {
        double m_median{}, m_tail{};
        std::uint64_t m_allocations{};
    };

    std::array<Timing, 2> measure_metadata(job_scheduler& scheduler,
                                           const own::shared_owner<const SceneViewInput>& input,
                                           optimized::SceneHost::PreparationScratch& scratch)
    {
        for (unsigned repeat = 0; repeat < 16; ++repeat)
        {
            for (unsigned order = 0; order < 2; ++order)
            {
                run_metadata(scheduler, input, scratch, ((repeat + order) % 2) != 0);
            }
        }
        std::array<std::array<double, 128>, 2> samples;
        for (std::size_t repeat = 0; repeat < samples[0].size(); ++repeat)
        {
            for (std::size_t order = 0; order < samples.size(); ++order)
            {
                const auto variant = (repeat + order) % samples.size();
                const auto start = std::chrono::steady_clock::now();
                run_metadata(scheduler, input, scratch, variant != 0);
                samples[variant][repeat] =
                    std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            }
        }
        std::array<Timing, 2> timings;
        for (std::size_t variant = 0; variant < samples.size(); ++variant)
        {
            auto& ordered = samples[variant];
            std::sort(ordered.begin(), ordered.end());
            allocations.store(0);
            count_allocations.store(true);
            run_metadata(scheduler, input, scratch, variant != 0);
            count_allocations.store(false);
            timings[variant] = {ordered[ordered.size() / 2], ordered[ordered.size() * 95 / 100], allocations.load()};
        }
        return timings;
    }

    void benchmark(unsigned workers)
    {
        thread_pool pool;
        job_scheduler scheduler(pool);
        optimized::SceneHost::PreparationScratch scratch;
        for (const auto chunks : {std::size_t{128}, std::size_t{512}, std::size_t{4096}})
        {
            for (const bool mixed : {false, true})
            {
                const own::shared_owner<const SceneViewInput> input = make_input(chunks, chunks, mixed);
                for (const bool parallel : {false, true})
                {
                    if (parallel)
                    {
                        scheduler.start(workers);
                    }
                    const auto timings = measure_metadata(scheduler, input, scratch);
                    const auto& baseline = timings[0];
                    const auto& optimized = timings[1];
                    std::cout << std::fixed << std::setprecision(3) << "METADATA_BENCH chunks=" << chunks
                              << " draws=" << chunks << " mixed=" << mixed << " workers=" << workers
                              << " scheduler_running=" << parallel << " baseline_p50_us=" << baseline.m_median
                              << " optimized_p50_us=" << optimized.m_median << " baseline_p95_us=" << baseline.m_tail
                              << " optimized_p95_us=" << optimized.m_tail
                              << " baseline_allocations=" << baseline.m_allocations
                              << " optimized_allocations=" << optimized.m_allocations << '\n';
                    if (parallel)
                    {
                        scheduler.shutdown();
                    }
                }
            }
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--benchmark")
        {
            benchmark(static_cast<unsigned>(std::stoul(argv[2])));
            return 0;
        }
        thread_pool pool;
        job_scheduler scheduler(pool);
        std::cout << "CHECK metadata: stopped scheduler" << std::endl;
        verify_metadata(scheduler);
        scheduler.start(1);
        std::cout << "CHECK metadata: one worker and failure drain" << std::endl;
        verify_metadata(scheduler);
        verify_metadata_drain(scheduler);
        scheduler.shutdown();
        scheduler.start(4);
        std::cout << "CHECK metadata: four workers and failure drain" << std::endl;
        verify_metadata(scheduler);
        verify_metadata_drain(scheduler);
        std::cout << "CHECK pipeline: owner overlap, dependencies and failure drains" << std::endl;
        verify_pipeline(scheduler);
        scheduler.shutdown();
        std::cout
            << "PASS metadata: baseline parity, zero/threshold/multichunk/tie inputs, retained scratch, "
               "durable-frame isolation, real scheduler 1/4 workers, inline fallback, owner/worker failure drains\n";
        return 0;
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
}
