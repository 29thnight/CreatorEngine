// The real preparation helper, JobScheduler and enkiTS run without an RHI device.
// Allocation counts cover the submitting thread; timing is a CPU submission probe.
#include "Engine/RenderEngine/MaterialGraphPreparationTasks.h"
#if CE_SCENE_PREPARATION_COMPARE
#include "BaselinePreparationTasks.h"
#endif
#include <cstdlib>
#include <iostream>
#include <new>

namespace
{
    thread_local bool count_allocations{};
    thread_local std::size_t allocation_count{};

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void count_allocation()
    {
        if (count_allocations)
        {
            ++allocation_count;
        }
    }
} // namespace

void* operator new(std::size_t size)
{
    count_allocation();
    if (void* memory = std::malloc(std::max<std::size_t>(size, 1)))
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
    count_allocation();
    const auto value = static_cast<std::size_t>(alignment);
    const auto padded = (std::max<std::size_t>(size, 1) + value - 1) / value * value;
    if (void* memory = std::aligned_alloc(value, padded))
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
    using material_graph::ScenePreparationTasks;
#if CE_SCENE_PREPARATION_COMPARE
    using BaselineTasks = baseline_preparation::ScenePreparationTasks;
#else
    using BaselineTasks = ScenePreparationTasks;
#endif
    const auto kMarker = ce::marker<"ScenePreparationCpuProbe">();

    struct throwing_copy
    {
        throwing_copy() = default;
        throwing_copy(const throwing_copy&) { throw std::runtime_error("callback copy failure"); }
        bool operator()(std::string&) const { return true; }
    };

    void verify_contracts(job_scheduler& scheduler)
    {
        scheduler.start(2);
        std::string error;
        std::array<unsigned, 4> values{};
        {
            std::promise<void> entered, release;
            auto ready = entered.get_future();
            const auto gate = release.get_future().share();
            ScenePreparationTasks tasks(scheduler, true);
            const bool admitted = tasks.Add(
                kMarker,
                [&](std::string&)
                {
                    entered.set_value();
                    gate.wait();
                    values[0] = 41;
                    return true;
                },
                error);
            const bool overlaps = admitted && ready.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            values[1] = 43;
            release.set_value();
            require(admitted && overlaps, "task did not start before owner work joined");
            require(tasks.Join(error) && values[0] == 41 && values[1] == 43, "join did not publish task output");
            require(tasks.Join(error), "join is not idempotent");
        }
        {
            ScenePreparationTasks tasks(scheduler, true);
            require(tasks.Add(
                        kMarker,
                        [](std::string& failure)
                        {
                            failure = "first failure";
                            return false;
                        },
                        error),
                    "failed callback was not admitted");
            require(tasks.Add(kMarker, [](std::string&) -> bool { throw 7; }, error), "throwing callback rejected");
            require(tasks.Add(
                        kMarker,
                        [&](std::string&)
                        {
                            values[2] = 47;
                            return true;
                        },
                        error),
                    "independent callback rejected");
            require(!tasks.Join(error) && error == "first failure" && values[2] == 47,
                    "failure did not drain independent work and preserve first error");
        }
        {
            ScenePreparationTasks tasks(scheduler, true);
            require(tasks.Add(kMarker, [](std::string&) -> bool { throw 7; }, error), "unknown failure rejected");
            require(!tasks.Join(error) && error == "Unknown scene preparation task failure.",
                    "unknown worker exception was not reported");
        }
        try
        {
            ScenePreparationTasks tasks(scheduler, true);
            require(tasks.Add(
                        kMarker,
                        [&](std::string&)
                        {
                            values[3] = 53;
                            return true;
                        },
                        error),
                    "owner-failure task rejected");
            throw std::runtime_error("owner failure");
        }
        catch (const std::runtime_error& failure)
        {
            require(std::string_view(failure.what()) == "owner failure" && values[3] == 53,
                    "owner exception did not drain the accepted task");
        }
        {
            ScenePreparationTasks tasks(scheduler, false);
            const auto owner = std::this_thread::get_id();
            bool ranInline{};
            auto mutableTask = [counter = 0, &ranInline, owner](std::string&) mutable
            {
                ranInline = ++counter == 1 && owner == std::this_thread::get_id();
                return true;
            };
            require(tasks.Add(kMarker, mutableTask, error) && ranInline && tasks.Join(error),
                    "mutable inline callable did not execute on its owner");
            require(!tasks.Add(
                        kMarker, [](std::string&) { return false; }, error) &&
                        error == "Scene preparation task failed.",
                    "empty inline failure message changed");
        }
        {
            std::atomic<unsigned> completed{};
            ScenePreparationTasks tasks(scheduler, true);
            for (unsigned index = 0; index < 16; ++index)
            {
                require(tasks.Add(
                            kMarker,
                            [&](std::string&)
                            {
                                ++completed;
                                return true;
                            },
                            error),
                        "bounded task admission rejected too early");
            }
            require(!tasks.Add(
                        kMarker, [](std::string&) { return true; }, error) &&
                        error == "Scene preparation task admission limit exceeded.",
                    "admission limit changed");
            require(tasks.Join(error) && completed == 16, "admission failure lost accepted work");
        }
        {
            unsigned completed{};
            ScenePreparationTasks tasks(scheduler, true);
            require(tasks.Add(
                        kMarker,
                        [&](std::string&)
                        {
                            ++completed;
                            return true;
                        },
                        error),
                    "copy-failure prerequisite rejected");
            throwing_copy task;
            bool admitted{};
            try
            {
                admitted = tasks.Add(kMarker, task, error);
            }
            catch (const std::runtime_error& failure)
            {
                // The baseline converts its std::function argument before Add's try.
                error = failure.what();
            }
            require(!admitted && error == "callback copy failure" && tasks.Join(error) && completed == 1,
                    "callback construction failure lost previously accepted work");
        }
        {
            std::array<unsigned, 3> outputs{};
            ScenePreparationTasks cascades(scheduler, true);
            ScenePreparationTasks setup(scheduler, true);
            require(setup.Add(
                        kMarker,
                        [&](std::string& failure)
                        {
                            if (!cascades.Add(
                                    kMarker,
                                    [&](std::string&)
                                    {
                                        outputs[0] = 59;
                                        return true;
                                    },
                                    failure))
                            {
                                return false;
                            }
                            outputs[1] = 61;
                            failure = "setup failure after child admission";
                            return false;
                        },
                        error),
                    "nested setup rejected");
            outputs[2] = 67;
            require(!setup.Join(error) && error == "setup failure after child admission" && outputs[1] == 61,
                    "setup failure did not publish the child handle");
            require(cascades.Join(error) && outputs[0] == 59 && outputs[2] == 67,
                    "accepted child outlived its captured snapshot");
        }
        {
            std::promise<void> firstEntered, secondEntered, release, assisted;
            auto firstReady = firstEntered.get_future();
            auto secondReady = secondEntered.get_future();
            auto completed = assisted.get_future();
            const auto gate = release.get_future().share();
            std::once_flag releaseOnce;
            const auto unblock = [&] { std::call_once(releaseOnce, [&] { release.set_value(); }); };
            auto first = scheduler.submit(
                [&]
                {
                    firstEntered.set_value();
                    gate.wait();
                });
            auto second = scheduler.submit(
                [&]
                {
                    secondEntered.set_value();
                    gate.wait();
                });
            const bool occupied = firstReady.wait_for(std::chrono::seconds(5)) == std::future_status::ready &&
                                  secondReady.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            ScenePreparationTasks tasks(scheduler, true);
            std::thread::id executor, helper;
            bool workerContext{};
            const bool admitted = tasks.Add(
                kMarker,
                [&](std::string&)
                {
                    executor = std::this_thread::get_id();
                    workerContext = thread_pool::is_worker_thread();
                    unblock();
                    return true;
                },
                error);
            bool joined{};
            std::thread owner(
                [&]
                {
                    helper = std::this_thread::get_id();
                    joined = tasks.Join(error);
                    assisted.set_value();
                });
            const bool helped = completed.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
            unblock();
            owner.join();
            first.wait();
            second.wait();
            require(occupied && admitted && helped && joined && executor == helper && workerContext,
                    "joining owner did not assist queued preparation work");
            require(!thread_pool::is_worker_thread(), "assistance leaked the worker context");
        }
        scheduler.shutdown();
        std::cout << "{\"kind\":\"contracts\",\"passed\":true}\n";
    }

    template<class Tasks>
    std::pair<double, std::size_t> measure_batch(job_scheduler& scheduler, bool parallel, bool largeCapture,
                                                 const std::array<std::uint64_t, 16>& payload,
                                                 std::atomic<std::uint64_t>& checksum)
    {
        constexpr std::size_t kTasks = 8;
        std::string error;
        Tasks tasks(scheduler, parallel);
        allocation_count = 0;
        count_allocations = true;
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t task = 0; task < kTasks; ++task)
        {
            bool admitted{};
            if (largeCapture)
            {
                admitted = tasks.Add(
                    kMarker,
                    [payload = payload, &checksum](std::string&) mutable
                    {
                        std::uint64_t sum{};
                        for (auto& value : payload)
                        {
                            sum += ++value;
                        }
                        checksum.fetch_add(sum, std::memory_order_relaxed);
                        return true;
                    },
                    error);
            }
            else
            {
                admitted = tasks.Add(
                    kMarker,
                    [&checksum](std::string&)
                    {
                        checksum.fetch_add(1, std::memory_order_relaxed);
                        return true;
                    },
                    error);
            }
            if (!admitted)
            {
                count_allocations = false;
                throw std::runtime_error(error);
            }
        }
        // Count only submission-owner allocations. Join can execute work on
        // this thread, so disable the allocation counter before assisting.
        count_allocations = false;
        require(tasks.Join(error), "measured task batch failed");
        const double elapsed =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
        return {elapsed, allocation_count};
    }

    void measure(job_scheduler& scheduler, bool parallel, bool largeCapture, unsigned workers, unsigned block)
    {
        constexpr std::size_t kTasks = 8;
        constexpr std::size_t kWarmup = 32;
        constexpr std::size_t kSamples = 129;
        std::array<std::array<double, kSamples>, 2> durations{};
        std::array<std::array<std::size_t, kSamples>, 2> allocations{};
        std::array<std::atomic<std::uint64_t>, 2> checksums{};
        std::array<std::uint64_t, 16> payload{};
        for (std::size_t index = 0; index < payload.size(); ++index)
        {
            payload[index] = index + workers + 1;
        }
        // The same active pool executes both unmodified helper bodies. Alternate
        // their order per sample so pool startup and worker placement are shared.
        for (std::size_t round = 0; round < kWarmup + kSamples; ++round)
        {
            for (std::size_t order = 0; order < 2; ++order)
            {
                const auto variant = (round + order + block) % 2;
                const auto sample =
                    variant == 0
                        ? measure_batch<BaselineTasks>(scheduler, parallel, largeCapture, payload, checksums[variant])
                        : measure_batch<ScenePreparationTasks>(scheduler, parallel, largeCapture, payload,
                                                               checksums[variant]);
                if (round >= kWarmup)
                {
                    durations[variant][round - kWarmup] = sample.first;
                    allocations[variant][round - kWarmup] = sample.second;
                }
            }
        }
        const auto expectedPerTask = largeCapture ? 16u * workers + 152u : 1u;
        for (std::size_t variant = 0; variant < 2; ++variant)
        {
            require(checksums[variant] == (kWarmup + kSamples) * kTasks * expectedPerTask,
                    "measured callbacks did not preserve their owned payloads");
            std::sort(durations[variant].begin(), durations[variant].end());
            std::sort(allocations[variant].begin(), allocations[variant].end());
            std::cout << "{\"kind\":\"submission\",\"variant\":\"" << (variant == 0 ? "baseline" : "prepared")
                      << "\",\"block\":" << block << ",\"workers\":" << workers
                      << ",\"parallel\":" << (parallel ? "true" : "false")
                      << ",\"capture_bytes\":" << (largeCapture ? 136 : 8) << ",\"tasks\":" << kTasks
                      << ",\"warmup\":" << kWarmup << ",\"samples\":" << kSamples
                      << ",\"batch_us_p50\":" << durations[variant][kSamples / 2]
                      << ",\"batch_us_p95\":" << durations[variant][kSamples * 95 / 100]
                      << ",\"owner_allocations_p50\":" << allocations[variant][kSamples / 2]
                      << ",\"checksum\":" << checksums[variant].load() << "}\n";
        }
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        thread_pool pool;
        job_scheduler scheduler(pool);
        verify_contracts(scheduler);
        if (argc > 1 && std::string_view(argv[1]) == "--benchmark")
        {
            for (unsigned workers : {2u, 8u})
            {
                scheduler.start(workers);
                for (unsigned block = 0; block < 3; ++block)
                {
                    measure(scheduler, false, false, workers, block);
                    measure(scheduler, false, true, workers, block);
                    measure(scheduler, true, false, workers, block);
                    measure(scheduler, true, true, workers, block);
                }
                scheduler.shutdown();
            }
        }
    }
    catch (const std::exception& failure)
    {
        count_allocations = false;
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
