#include "JobScheduler.h"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <type_traits>

int main()
{
    using namespace std::chrono_literals;
#ifdef WORKER_POOL_LIFETIME_STRESS
    constexpr int rounds = 4;
#else
    constexpr int rounds = 160;
#endif
    thread_pool pool;
    job_scheduler scheduler(pool);
    unsigned checks = 0;
    const auto require = [&](bool condition, const char* label) {
        ++checks;
        if (!condition)
        {
            scheduler.shutdown(); // Keep task-referenced locals alive on failure.
            throw std::runtime_error(label);
        }
    };
    try
    {
        static_assert(!std::is_copy_constructible_v<job_group>);
        static_assert(!std::is_copy_constructible_v<thread_pool>);
        unsigned inline_fallback = 0;
        bool rejected = false;
        try
        {
            scheduler.submit([&] { ++inline_fallback; });
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected && inline_fallback == 0, "stopped submission must not execute inline");
        scheduler.start(4);
        scheduler.start(1);
        require(pool.size() == 4 && scheduler.is_running(), "persistent idempotent startup");

        std::atomic<bool> release{false}, entered{false};
        std::atomic<unsigned> completed{0};
        auto blocked = scheduler.submit([&] {
            entered = true;
            while (!release.load())
                std::this_thread::yield();
            ++completed;
        });
        while (!entered.load())
            std::this_thread::yield();
        std::jthread unblock([&] {
            std::this_thread::sleep_for(30ms);
            release = true;
        });
        blocked.wait();
        const bool barrier_held = completed == 1;
        unblock.join();
        require(barrier_held, "barrier returned before independent completion");

        // A completed foreground group must not wait for unrelated ongoing work.
        auto foreground = scheduler.submit([] {});
        foreground.wait();
        release = false;
        entered = false;
        auto background = scheduler.submit([&] {
            entered = true;
            while (!release.load())
                std::this_thread::yield();
        });
        while (!entered.load())
            std::this_thread::yield();
        std::jthread release_background([&] {
            std::this_thread::sleep_for(100ms);
            release = true;
        });
        foreground.wait();
        const bool isolated_wait = !release.load();
        release_background.join();
        background.wait();
        require(isolated_wait, "group wait included unrelated work");

        std::mutex ids_mutex;
        std::set<std::thread::id> worker_ids;
        for (int round = 0; round < rounds; ++round)
        {
            std::array<std::atomic<unsigned>, 64> slots{};
            job_group group;
            for (unsigned i = 0; i < slots.size(); ++i)
                group.add([&, i] {
                    ++slots[i];
                    std::lock_guard lock(ids_mutex);
                    worker_ids.insert(std::this_thread::get_id());
                });
            scheduler.submit(std::move(group)).wait();
            for (const auto& slot : slots)
                require(slot == 1, "batch exactly once");
        }
        require(worker_ids.size() <= 4 && !worker_ids.contains(std::this_thread::get_id()),
                "workers reused across batches");
        std::atomic<unsigned> ran{0}, on_submitter{0};
        std::array<job_handle, 4> external;
        std::vector<std::jthread> producers;
        for (unsigned p = 0; p < external.size(); ++p)
            producers.emplace_back([&, p] {
                const auto submitter = std::this_thread::get_id();
                job_group group;
                for (int i = 0; i < 64; ++i)
                    group.add([&, submitter] {
                        ++ran;
                        if (std::this_thread::get_id() == submitter)
                            ++on_submitter;
                    });
                external[p] = scheduler.submit(std::move(group));
            });
        producers.clear();
        for (auto& handle : external)
            handle.wait();
        require(ran == 256 && on_submitter == 0, "external producers never execute inline");

        auto capture = std::make_shared<int>(42);
        std::weak_ptr<int> weak = capture;
        auto retained = scheduler.submit([capture] {});
        capture.reset();
        retained.wait();
        require(weak.expired(), "retained handle must not retain callback captures");

        std::array<std::atomic<unsigned>, 73> ranges{};
        scheduler
            .parallel_for(ranges.size(), 7,
                          [&](std::size_t begin, std::size_t end) {
                              for (auto i = begin; i < end; ++i)
                                  ++ranges[i];
                          })
            .wait();
        for (const auto& slot : ranges)
            require(slot == 1, "parallel_for tail coverage");
        scheduler.parallel_for(0, 1, [](auto, auto) { throw std::runtime_error("empty range executed"); }).wait();
        rejected = false;
        try
        {
            scheduler.parallel_for(1, 0, [](auto, auto) {});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "zero grain rejected");

        release = false;
        std::atomic<unsigned> stages{0};
        auto root = scheduler.submit([&] {
            while (!release.load())
                std::this_thread::yield();
            stages = 1;
        });
        std::array<job_handle, 2> branches;
        for (unsigned i = 0; i < branches.size(); ++i)
        {
            job_group branch;
            branch.add([&, i] {
                if (!(stages.load() & 1))
                    throw std::runtime_error("dependency ran early");
                stages.fetch_or(2u << i);
            });
            branches[i] = scheduler.submit_after(std::span(&root, 1), std::move(branch));
        }
        job_group tail;
        tail.add([&] {
            if (stages != 7)
                throw std::runtime_error("fan-in ran early");
            stages = 15;
        });
        auto graph = scheduler.submit_after(branches, std::move(tail));
        const bool pending_graph = !graph.is_complete();
        release = true;
        graph.wait();
        require(pending_graph && stages == 15, "dependency fan-out and fan-in");

        release = false;
        auto chain = scheduler.submit([&] {
            while (!release.load())
                std::this_thread::yield();
        });
        for (int i = 0; i < 4096; ++i)
            chain = scheduler.submit_after(std::span(&chain, 1), job_group{});
        release = true;
        chain.wait();
        require(chain.is_complete(), "deep dependency chain completes without recursive stack growth");

        auto failed = scheduler.submit([] { throw std::runtime_error("expected failure"); });
        capture = std::make_shared<int>(42);
        weak = capture;
        job_group skipped;
        skipped.add([capture] { throw std::runtime_error("dependent body must be skipped"); });
        auto dependent = scheduler.submit_after(std::span(&failed, 1), std::move(skipped));
        capture.reset();
        bool caught = false;
        try
        {
            dependent.wait();
        }
        catch (const std::runtime_error& error)
        {
            caught = std::string_view(error.what()) == "expected failure";
        }
        require(caught && weak.expired(), "dependency failure propagation and capture release");
        scheduler.submit([] {}).wait(); // Errors belong to handles, not the pool.

        job_handle child;
        release = false;
        std::atomic<bool> worker_wait_rejected{false};
        scheduler
            .submit([&] {
                child = scheduler.submit([&] {
                    while (!release.load())
                        std::this_thread::yield();
                });
                try
                {
                    child.wait();
                }
                catch (const std::logic_error&)
                {
                    worker_wait_rejected = true;
                }
                release = true;
            })
            .wait();
        child.wait();
        require(worker_wait_rejected, "worker blocking wait rejected");

        rejected = false;
        job_handle invalid;
        try
        {
            scheduler.submit_after(std::span(&invalid, 1), job_group{});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, "invalid dependency rejected");

        release = false;
        ran = 0;
        auto shutdown_root = scheduler.submit([&] {
            while (!release.load())
                std::this_thread::yield();
            ++ran;
        });
        job_group shutdown_tail;
        shutdown_tail.add([&] { ++ran; });
        auto drained = scheduler.submit_after(std::span(&shutdown_root, 1), std::move(shutdown_tail));
        std::jthread shutdown_release([&] {
            std::this_thread::sleep_for(30ms);
            release = true;
        });
        scheduler.shutdown();
        shutdown_release.join();
        require(ran == 2 && drained.is_complete() && !pool.is_running(), "shutdown drains accepted dependent work");
        scheduler.shutdown();
        rejected = false;
        try
        {
            scheduler.submit([] {});
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        require(rejected, "closed scheduler rejects new jobs");
        for (int i = 0; i < 4; ++i)
        {
            scheduler.start(4);
            scheduler.submit([] {}).wait();
            scheduler.shutdown();
            require(!pool.is_running(), "restart lifecycle");
        }
        std::cout << "JOB_SCHEDULER_OK checks=" << checks << " workers=4 backend=enkiTS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "JOB_SCHEDULER_FAILED " << error.what() << '\n';
        return 1;
    }
}
