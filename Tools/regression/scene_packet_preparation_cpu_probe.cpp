#include "Engine/RenderEngine/RenderArena.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Arena.h"
#else
import Creator.Core.Arena;
#endif
// 실제 packet 작성/합류 본문을 실행한다. GPU plan/RHI만 작은 계약 fixture이다.
#include "Engine/RenderEngine/MaterialGraphPreparationTasks.h"
#include "BaselinePreparationTasks.h"
#include <cstdlib>
#include <iostream>

namespace
{
    std::atomic<bool> count_allocations{};
    std::atomic<std::uint64_t> allocations{};
    std::uint64_t checks{};
    void require(bool value, const char* message)
    {
        ++checks;
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }
} // namespace

void scene_packet_count_allocation()
{
    if (count_allocations.load(std::memory_order_relaxed))
    {
        allocations.fetch_add(1, std::memory_order_relaxed);
    }
}
void* operator new(std::size_t size)
{
    scene_packet_count_allocation();
    if (void* memory = std::malloc(std::max(std::size_t{1}, size)))
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
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
void* operator new(std::size_t size, std::align_val_t alignment)
{
    scene_packet_count_allocation();
    void* memory{};
    if (posix_memalign(&memory, static_cast<std::size_t>(alignment), std::max(std::size_t{1}, size)) == 0)
    {
        return memory;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return ::operator new(size, alignment);
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

namespace
{
    struct Handle
    {
        unsigned m_id{};
        bool IsValid() const { return m_id != 0; }
    };
    struct BufferSlice
    {
        Handle buffer;
        std::uint64_t offset{}, size{};
        void* cpuAddress{};
        bool IsWritable() const { return cpuAddress != nullptr; }
    };
    struct GpuGeometryVisibility
    {
        struct Frame
        {
            unsigned m_id{};
            std::uint32_t MeshletOutputOffset(unsigned bin) const { return m_id * 4096 + bin * 16; }
        };
    };
    struct MeshSurfaceBatch
    {
        unsigned m_count{};
        unsigned MeshletCount() const { return m_count; }
    };

    unsigned packet_id(std::string_view name)
    {
        if (name.ends_with("Surface"))
        {
            return 0;
        }
        if (name.ends_with("Depth"))
        {
            return 1;
        }
        const auto cascade = static_cast<unsigned>(name.back() - '0');
        if (name.find("Static") != name.npos)
        {
            return 2 + cascade;
        }
        return 5 + cascade * 2 + (name.find("Final") != name.npos);
    }

    struct Device
    {
        std::vector<std::byte> m_upload{8 * 1024 * 1024};
        std::atomic<std::size_t> m_cursor{};
        std::atomic<bool> m_release{true};
        std::atomic<unsigned> m_entered{};
        std::array<std::atomic<unsigned>, 11> m_completed{};
        std::array<unsigned, 11> m_failures{};
        bool m_ownerFailure{}, m_growWhilePending{};

        BufferSlice AllocateUpload(std::size_t bytes, std::size_t alignment)
        {
            const auto padded = (bytes + alignment - 1) / alignment * alignment;
            const auto offset = m_cursor.fetch_add(padded, std::memory_order_relaxed);
            if (offset > m_upload.size() || bytes > m_upload.size() - offset)
            {
                return {};
            }
            return {{1}, offset, bytes, m_upload.data() + offset};
        }
        void AwaitAdmission()
        {
            m_entered.fetch_add(1, std::memory_order_release);
            while (!m_release.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }
        }
        void Release() { m_release.store(true, std::memory_order_release); }
        void Reset()
        {
            m_cursor = 0;
            m_entered = 0;
            for (auto& completed : m_completed)
            {
                completed = 0;
            }
        }
    };
    struct PacketDispatchWork
    {
        Handle arguments;
        std::vector<std::uint32_t> m_capacities;
        bool Prepare(Device& device, const std::shared_ptr<unsigned>&, unsigned, unsigned,
                     std::span<const std::uint32_t> capacities, unsigned, unsigned, std::string_view name,
                     std::string& error)
        {
            const auto id = packet_id(name);
            ++device.m_completed[id];
            if (device.m_failures[id] == 1)
            {
                error = "failure-" + std::to_string(id);
                return false;
            }
            if (device.m_failures[id] == 2)
            {
                throw std::runtime_error("exception-" + std::to_string(id));
            }
            if (device.m_failures[id] == 3)
            {
                throw id;
            }
            m_capacities.assign(capacities.begin(), capacities.end());
            arguments = {id + 1};
            return true;
        }
    };
    struct IndirectMaterialBatch
    {
#include "PacketDraw.inl"
#include "PacketBucket.inl"
        Device* device{};
        std::shared_ptr<unsigned> pool;
        std::shared_ptr<const GpuGeometryVisibility::Frame> visibility;
        std::array<unsigned, 6> packetPipelines{};
        std::unique_ptr<PacketDispatchWork> packetWork;
        std::vector<Bucket> buckets;
        BufferSlice commands;
        Handle output;
        unsigned commandCount{};
        std::string name;
#include "UploadPackets.inl"
    };
    struct Pipeline
    {
        Device* m_device{};
        unsigned GetGeneration() const
        {
            m_device->AwaitAdmission();
            return 1;
        }
    };
    struct Program
    {
        struct Generation
        {
            struct Cooked
            {
                struct Product
                {
                    struct ProgramKey
                    {
                        std::string_view semanticKey;
                    } program;
                } product;
            } cooked;
        };
        std::shared_ptr<const Generation> generation;
    };
    struct Draw
    {
        std::shared_ptr<const Program> program;
        std::shared_ptr<const MeshSurfaceBatch> geometry;
        unsigned geometryRecord{}, cameraMeshletBin{}, shadowMeshletBin{}, materialBucket{};
        bool doubleSided{}, blended{}, shadowEnabled{}, staticShadow{}, bucketRepresentative{}, m_meshlets{true};
    };
    using FixtureDraw = Draw;
    struct Frame
    {
        using Draw = FixtureDraw;
    };
    struct Candidate
    {
        std::vector<Draw> draws, shadowDraws;
        struct VisibilityStage
        {
            std::shared_ptr<const IndirectMaterialBatch> materialCommands;
        } visibilityStage;
        struct OccluderStage
        {
            std::shared_ptr<const IndirectMaterialBatch> depthCommands;
        } occluderStage;
        struct FirstCullStage
        {
            std::shared_ptr<const GpuGeometryVisibility::Frame> cameraDepthMeshlets;
        } firstCullStage;
        struct FinalCullStage
        {
            std::shared_ptr<const GpuGeometryVisibility::Frame> cameraMeshlets;
        } finalCullStage;
        struct ShadowCullStage
        {
            std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> meshlets;
        } shadowFirstCullStage, shadowFinalCullStage;
        struct ShadowCacheStage
        {
            bool hit{};
            std::array<std::shared_ptr<const GpuGeometryVisibility::Frame>, 3> meshlets;
            std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> commands;
        } shadowCacheStage;
        struct ShadowOccluderStage
        {
            std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> commands;
        } shadowOccluderStage;
        struct ShadowRasterStage
        {
            std::array<std::shared_ptr<const IndirectMaterialBatch>, 3> shadowCommands;
        } shadowRasterStage;
        bool UsesMeshlets(const Draw& draw) const { return draw.m_meshlets; }
    };
    struct SceneData
    {
        std::vector<unsigned> recordPages;
    };
    struct PreparationScratch
    {
#include "PacketPreparationScratch.inl"
    };
    struct ScratchWork
    {
        ce::RenderArenaLease m_storage;
    };
    struct ReleaseOnExit
    {
        Device* m_device{};
        ~ReleaseOnExit() { m_device->Release(); }
    };
    thread_local std::array<std::size_t, 2> indexed_admissions{};
    thread_local std::size_t indexed_admission_count{};
    class TrackingPreparationTasks
    {
      public:
        TrackingPreparationTasks(job_scheduler& scheduler, bool parallel) : m_tasks(scheduler, parallel) {}
        template<class F>
        bool AddIndexed(ce::marker_id marker, std::size_t count, F&& task, std::string& error)
        {
            require(indexed_admission_count < indexed_admissions.size(), "too many packet admission phases");
            indexed_admissions[indexed_admission_count++] = count;
            return m_tasks.AddIndexed(marker, count, std::forward<F>(task), error);
        }
        bool Join(std::string& error) { return m_tasks.Join(error); }

      private:
        material_graph::ScenePreparationTasks m_tasks;
    };

    struct Fixture
    {
        job_scheduler& scheduler_;
        Device m_device;
        Device* device_{&m_device};
        std::shared_ptr<unsigned> bufferPool_{std::make_shared<unsigned>(1)};
        std::array<Pipeline, 6> packetPipelines_;
        std::unique_ptr<PreparationScratch> m_preparationScratch{std::make_unique<PreparationScratch>()};
        std::shared_ptr<Candidate> candidate;
        std::shared_ptr<SceneData> sceneData{std::make_shared<SceneData>()};
        ScratchWork scratch;
        bool parallelPreparation{};
        std::string error;

        explicit Fixture(job_scheduler& scheduler) : scheduler_(scheduler)
        {
            for (auto& pipeline : packetPipelines_)
            {
                pipeline.m_device = device_;
            }
        }
        void Initialize(std::size_t count, bool statics, bool hit, bool parallel)
        {
            candidate = std::make_shared<Candidate>();
            sceneData->recordPages.assign(count + 1, 0);
            scratch.m_storage = m_preparationScratch->m_pool.Acquire();
            parallelPreparation = parallel;
            candidate->shadowCacheStage.hit = hit;
            candidate->firstCullStage.cameraDepthMeshlets = std::make_shared<GpuGeometryVisibility::Frame>(11);
            candidate->finalCullStage.cameraMeshlets = std::make_shared<GpuGeometryVisibility::Frame>(13);
            for (unsigned cascade = 0; cascade < 3; ++cascade)
            {
                candidate->shadowCacheStage.meshlets[cascade] =
                    std::make_shared<GpuGeometryVisibility::Frame>(17 + cascade);
                candidate->shadowFirstCullStage.meshlets[cascade] =
                    std::make_shared<GpuGeometryVisibility::Frame>(23 + cascade);
                candidate->shadowFinalCullStage.meshlets[cascade] =
                    std::make_shared<GpuGeometryVisibility::Frame>(29 + cascade);
            }
            constexpr std::array keys{"alpha", "beta", "alpha.detail", "gamma", "alpha.masked"};
            for (std::size_t index = 0; index < count; ++index)
            {
                auto generation = std::make_shared<Program::Generation>();
                generation->cooked.product.program.semanticKey = keys[index % keys.size()];
                auto program = std::make_shared<Program>();
                program->generation = std::move(generation);
                Draw draw;
                draw.program = std::move(program);
                draw.geometry = std::make_shared<MeshSurfaceBatch>(static_cast<unsigned>(index % 97 + 1));
                draw.geometryRecord = static_cast<unsigned>(index + 1);
                draw.cameraMeshletBin = static_cast<unsigned>(index);
                draw.shadowMeshletBin = static_cast<unsigned>(count - index);
                draw.doubleSided = index % 3 == 0;
                draw.blended = index % 11 == 7;
                draw.shadowEnabled = index % 7 != 3;
                draw.staticShadow = statics && index % 2 == 0;
                draw.m_meshlets = index % 13 != 8;
                sceneData->recordPages[index + 1] = static_cast<unsigned>(index % 3);
                candidate->draws.push_back(draw);
                candidate->shadowDraws.push_back(std::move(draw));
            }
            error.clear();
            m_device.Reset();
        }
        void AfterCameraAdmission()
        {
            if (!m_device.m_release.load() && candidate->draws.size())
            {
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (!m_device.m_entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < until)
                {
                    std::this_thread::yield();
                }
                require(m_device.m_entered != 0, "camera batch did not overlap owner preparation");
            }
            if (m_device.m_ownerFailure)
            {
                throw std::runtime_error("owner failure after camera admission");
            }
        }
        void BeforePacketJoin()
        {
            if (m_device.m_growWhilePending)
            {
                std::memset(scratch.m_storage->Resource(2).allocate(256 * 1024, 16), 0x5a, 256 * 1024);
            }
            m_device.Release();
        }
        bool Run(bool optimized)
        {
            if (optimized)
            {
                indexed_admission_count = 0;
                using ScenePreparationTasks = TrackingPreparationTasks;
#include "CurrentPackets.inl"
            }
            else
            {
                using baseline_preparation::ScenePreparationTasks;
#include "BaselinePackets.inl"
            }
        }
        std::array<std::shared_ptr<const IndirectMaterialBatch>, 11> Outputs() const
        {
            return {candidate->visibilityStage.materialCommands,    candidate->occluderStage.depthCommands,
                    candidate->shadowCacheStage.commands[0],        candidate->shadowCacheStage.commands[1],
                    candidate->shadowCacheStage.commands[2],        candidate->shadowOccluderStage.commands[0],
                    candidate->shadowRasterStage.shadowCommands[0], candidate->shadowOccluderStage.commands[1],
                    candidate->shadowRasterStage.shadowCommands[1], candidate->shadowOccluderStage.commands[2],
                    candidate->shadowRasterStage.shadowCommands[2]};
        }
    };

    void compare_outputs(const Fixture& before, const Fixture& after)
    {
        const auto left = before.Outputs(), right = after.Outputs();
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            require(bool(left[index]) == bool(right[index]), "packet presence differs");
            if (!left[index])
            {
                continue;
            }
            require(before.m_device.m_completed[index] == 1 && after.m_device.m_completed[index] == 1,
                    "logical packet skipped or executed twice");
            require(left[index]->name == right[index]->name &&
                        left[index]->commands.size == right[index]->commands.size,
                    "packet identity or byte size differs");
            require(std::memcmp(left[index]->commands.cpuAddress, right[index]->commands.cpuAddress,
                                left[index]->commands.size) == 0,
                    "uploaded packet bytes differ");
            require(left[index]->packetWork->m_capacities == right[index]->packetWork->m_capacities,
                    "packet bucket capacities differ");
            require(left[index]->buckets.size() == right[index]->buckets.size(), "bucket count differs");
            for (std::size_t bucket = 0; bucket < left[index]->buckets.size(); ++bucket)
            {
                const auto& a = left[index]->buckets[bucket];
                const auto& b = right[index]->buckets[bucket];
                require(a.representative == b.representative && a.count == b.count && a.roots == b.roots,
                        "bucket identity/representative/order differs");
            }
        }
        for (std::size_t index = 0; index < before.candidate->draws.size(); ++index)
        {
            const auto& a = before.candidate->draws[index];
            const auto& b = after.candidate->draws[index];
            require(a.materialBucket == b.materialBucket && a.bucketRepresentative == b.bucketRepresentative,
                    "camera canonical bucket publication differs");
        }
    }

    void verify(job_scheduler& scheduler)
    {
        for (unsigned workers : {1u, 2u, 4u, 8u})
        {
            scheduler.start(workers);
            Fixture before(scheduler), after(scheduler);
            for (const auto count : {0u, 1u, 17u, 97u, 4096u})
            {
                for (const bool statics : {false, true})
                {
                    for (const bool hit : {false, true})
                    {
                        for (const bool parallel : {false, true})
                        {
                            before.Initialize(count, statics, hit, parallel);
                            after.Initialize(count, statics, hit, parallel);
                            before.m_device.m_release = !(parallel && count);
                            after.m_device.m_release = !(parallel && count);
                            before.m_device.m_growWhilePending = after.m_device.m_growWhilePending = true;
                            require(before.Run(false), "baseline production packet preparation failed");
                            require(after.Run(true), "optimized production packet preparation failed");
                            compare_outputs(before, after);
                            // 11 logical scratch writers are reusable; overwrites must not alter uploaded bytes.
                            for (auto& records : after.m_preparationScratch->m_packetRecords)
                            {
                                records.resize(512);
                                std::memset(records.data(), 0xc3, records.size() * sizeof(records.front()));
                            }
                            compare_outputs(before, after);
                        }
                    }
                }
            }
            for (unsigned first : {0u, 1u, 2u, 5u, 10u})
            {
                for (unsigned kind : {1u, 2u, 3u})
                {
                    for (const bool parallel : {false, true})
                    {
                        before.Initialize(97, true, false, parallel);
                        after.Initialize(97, true, false, parallel);
                        before.m_device.m_failures.fill(0);
                        after.m_device.m_failures.fill(0);
                        before.m_device.m_failures[first] = after.m_device.m_failures[first] = kind;
                        before.m_device.m_failures[10] = after.m_device.m_failures[10] = kind;
                        std::array<std::optional<unsigned>, 2> unknown;
                        std::array<bool, 2> succeeded{};
                        for (unsigned variant = 0; variant < 2; ++variant)
                        {
                            try
                            {
                                succeeded[variant] = variant ? after.Run(true) : before.Run(false);
                            }
                            catch (unsigned value)
                            {
                                unknown[variant] = value;
                            }
                        }
                        require(!succeeded[0] && !succeeded[1], "packet failure was lost");
                        require(before.error == after.error && unknown[0] == unknown[1],
                                "canonical first failure or inline unknown exception changed");
                        for (std::size_t index = 0; index < after.m_device.m_completed.size(); ++index)
                        {
                            if (parallel)
                            {
                                require(after.m_device.m_completed[index] == 1,
                                        "failed indexed batch did not drain every independent packet");
                            }
                            else
                            {
                                require(after.m_device.m_completed[index] == before.m_device.m_completed[index],
                                        "inline failure did not stop before the next logical packet");
                            }
                        }
                    }
                }
            }
            after.m_device.m_failures.fill(0);
            after.Initialize(97, true, false, true);
            after.m_device.m_ownerFailure = true;
            after.m_device.m_release = false;
            try
            {
                after.Run(true);
                require(false, "owner exception did not escape");
            }
            catch (const std::runtime_error& failure)
            {
                require(std::string_view(failure.what()) == "owner failure after camera admission",
                        "owner exception identity changed");
            }
            require(after.m_device.m_completed[0] == 1 && after.m_device.m_completed[1] == 1,
                    "owner exception destroyed packet storage before camera tasks drained");
            scheduler.shutdown();
        }
        std::cout << "{\"kind\":\"contracts\",\"checks\":" << checks << ",\"passed\":true}\n";
    }

    void verify_allocations(job_scheduler& scheduler)
    {
        for (const auto workers : {4u, 8u})
        {
            scheduler.start(workers);
            Fixture before(scheduler), after(scheduler);
            for (const auto count : {97u, 4096u})
            {
                for (const bool hit : {false, true})
                {
                    constexpr std::size_t kWarmup = 16, kSamples = 17;
                    std::array<std::array<std::uint64_t, kSamples>, 2> samples{};
                    for (std::size_t round = 0; round < kWarmup + kSamples; ++round)
                    {
                        for (std::size_t order = 0; order < 2; ++order)
                        {
                            const auto variant = (round + order) % 2;
                            auto& fixture = variant ? after : before;
                            fixture.Initialize(count, true, hit, true);
                            allocations = 0;
                            count_allocations = true;
                            const auto success = fixture.Run(variant != 0);
                            count_allocations = false;
                            require(success, "warm allocation contract failed");
                            if (round >= kWarmup)
                            {
                                samples[variant][round - kWarmup] = allocations.load();
                            }
                        }
                    }
                    for (auto& sample : samples)
                    {
                        std::sort(sample.begin(), sample.end());
                    }
                    require(samples[1][kSamples / 2] < samples[0][kSamples / 2],
                            "warm packet path did not reduce heap allocation requests");
                    std::cout << "{\"kind\":\"warm-allocations\",\"workers\":" << workers << ",\"draws\":" << count
                              << ",\"staticCacheHit\":" << (hit ? "true" : "false")
                              << ",\"beforeMin\":" << samples[0].front()
                              << ",\"beforeMedian\":" << samples[0][kSamples / 2]
                              << ",\"beforeMax\":" << samples[0].back() << ",\"afterMin\":" << samples[1].front()
                              << ",\"afterMedian\":" << samples[1][kSamples / 2]
                              << ",\"afterMax\":" << samples[1].back() << ",\"samples\":" << kSamples << "}\n";
                }
            }
            scheduler.shutdown();
        }
    }

    void verify_injected_scheduler()
    {
        require(!ce::get_job_scheduler().is_running() && ce::get_thread_pool().size() == 0,
                "injected scheduler test requires the global pool to be inactive");
        thread_pool pool;
        job_scheduler scheduler(pool);
        for (const auto workers : {4u, 8u})
        {
            scheduler.start(workers);
            Fixture fixture(scheduler);
            fixture.Initialize(97, false, true, true);
            require(scheduler.worker_count() == workers, "worker count came from a different scheduler");
            require(fixture.Run(true), "injected scheduler packet preparation failed");
            require(indexed_admission_count == 2 && indexed_admissions[0] == 2 &&
                        indexed_admissions[1] == std::min<std::size_t>(6, workers),
                    "packet participants used the inactive global pool instead of the injected owner");
            std::cout << "{\"kind\":\"injected-scheduler\",\"workers\":" << workers
                      << ",\"globalWorkers\":" << ce::get_thread_pool().size()
                      << ",\"cameraParticipants\":" << indexed_admissions[0]
                      << ",\"shadowParticipants\":" << indexed_admissions[1] << ",\"passed\":true}\n";
            scheduler.shutdown();
        }
    }
} // namespace

int main()
{
    try
    {
        auto& scheduler = ce::get_job_scheduler();
        verify(scheduler);
        verify_allocations(scheduler);
        verify_injected_scheduler();
        return 0;
    }
    catch (const std::exception& failure)
    {
        count_allocations = false;
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
