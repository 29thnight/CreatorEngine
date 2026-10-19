// The production declaration helper and scheduler run against the pinned enkiTS dependency.
// This CPU contract probe does not create an RHI device or exercise the Windows profiler.
#include "../../Engine/RenderEngine/Render/Graph/RenderGraphDeclarationTask.h"
#include <iostream>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void wait_flag(const std::atomic<bool>& flag)
    {
        while (!flag.load(std::memory_order_acquire))
        {
            std::this_thread::yield();
        }
    }

    void exercise(job_scheduler& scheduler, std::size_t workers)
    {
        scheduler.start(workers);
        std::array<unsigned, 257> visits{};
        const auto fill = [&](std::size_t index) { ++visits[index]; };
        {
            render_graph_declaration_task task(scheduler);
            task.start(visits.size(), true, fill);
            task.join();
            task.join();
            task.start(0, true, fill);
        }
        require(std::ranges::all_of(visits, [](unsigned count) { return count == 1; }),
                "indexed work did not run exactly once");

        std::atomic<unsigned> count{};
        const auto increment = [&](std::size_t) { count.fetch_add(1, std::memory_order_relaxed); };
        {
            render_graph_declaration_task task(scheduler);
            task.start(43, true, increment);
        }
        require(count.load() == 43, "normal scope exit did not drain accepted work");

        count.store(0);
        const auto failing = [&](std::size_t index)
        {
            count.fetch_add(1, std::memory_order_relaxed);
            if (index == 0)
            {
                throw std::runtime_error("worker failure");
            }
        };
        bool caught{};
        {
            render_graph_declaration_task task(scheduler);
            task.start(113, true, failing);
            try
            {
                task.join();
            }
            catch (const std::runtime_error& error)
            {
                caught = std::string_view(error.what()) == "worker failure";
            }
        }
        require(caught && count.load() == 113, "worker failure escaped before accepted work drained");

        count.store(0);
        caught = false;
        try
        {
            render_graph_declaration_task task(scheduler);
            task.start(127, true, failing);
            throw std::runtime_error("owner failure");
        }
        catch (const std::runtime_error& error)
        {
            caught = std::string_view(error.what()) == "owner failure";
        }
        require(caught && count.load() == 127, "destructor failed to preserve owner exception and drain workers");

        std::atomic<bool> release{};
        const auto blocked = [&](std::size_t) { wait_flag(release); };
        {
            render_graph_declaration_task task(scheduler);
            task.start(1, true, blocked);
            caught = false;
            try
            {
                task.start(1, true, fill);
            }
            catch (const std::logic_error&)
            {
                caught = true;
            }
            release.store(true, std::memory_order_release);
            task.join();
        }
        require(caught, "second admission was not rejected");

        // Rejection after job admission must complete its handle without executing
        // the borrowed callable. The real pool rejects this range before allocating work.
        if constexpr (sizeof(std::size_t) > sizeof(std::uint32_t))
        {
            count.store(0);
            caught = false;
            render_graph_declaration_task task(scheduler);
            task.start(std::size_t((std::numeric_limits<std::uint32_t>::max)()) + 1, true, increment);
            try
            {
                task.join();
            }
            catch (const std::invalid_argument&)
            {
                caught = true;
            }
            require(caught && count.load() == 0, "dispatch rejection did not complete without running callbacks");
            task.start(3, false, increment);
            require(count.load() == 3, "joined failure did not allow inline reuse");
        }

        count.store(0);
        const job_handle outer = scheduler.submit(
            [&]
            {
                require(thread_pool::is_worker_thread(), "outer task did not run inside pool");
                render_graph_declaration_task task(scheduler);
                task.start(31, true, increment);
                task.join();
                require(count.load() == 31, "nested task did not finish inline");
            });
        outer.wait();
        require(count.load() == 31, "nested execution lost work");
        scheduler.shutdown();
        std::cout << "PASS real scheduler workers=" << workers
                  << ": indexed, join, scope drain, worker error, unwind drain, duplicate admission, "
                     "dispatch rejection, worker inline\n";
    }

    void exercise_owner_assist(job_scheduler& scheduler)
    {
        scheduler.start(1);
        const auto owner = std::this_thread::get_id();
        std::mutex scratch, independent;
        std::atomic<bool> entered{}, release{};
        unsigned nestedCount{};
        const auto increment = [&](std::size_t) { ++nestedCount; };
        const auto assisted = [&](std::size_t)
        {
            // Release the occupied worker even if an assertion throws.
            struct release_worker
            {
                std::atomic<bool>& flag_;
                ~release_worker() { flag_.store(true, std::memory_order_release); }
            } releaseWorker{release};
            require(std::this_thread::get_id() == owner, "join did not assist while the only worker was occupied");
            require(thread_pool::is_worker_thread(), "assisted callback did not carry worker status");
            render_graph_declaration_lock duplicate(scratch);
            require(!duplicate.owns_lock(), "assisted callback acquired its owner's scratch");
            {
                render_graph_declaration_lock separate(independent);
                require(separate.owns_lock(), "independent nested scratch was not available");
                render_graph_declaration_lock original(scratch);
                require(!original.owns_lock(), "nested scratch hid an earlier active owner");
            }
            render_graph_declaration_lock stillOwned(scratch);
            require(!stillOwned.owns_lock(), "nested lock destruction lost its predecessor");
            render_graph_declaration_task nested(scheduler);
            nested.start(19, true, increment);
            nested.join();
            require(nestedCount == 19, "assisted callback tried to wait for nested pool work");
        };
        {
            render_graph_declaration_lock ownerLock(scratch);
            require(ownerLock.owns_lock(), "owner could not acquire unused scratch");
            const job_handle contended = scheduler.submit(
                [&]
                {
                    require(std::this_thread::get_id() != owner && thread_pool::is_worker_thread(),
                            "contention callback did not run on the separate worker");
                    render_graph_declaration_lock unavailable(scratch);
                    require(!unavailable.owns_lock(), "worker acquired scratch still held by owner");
                    render_graph_declaration_lock available(independent);
                    require(available.owns_lock(), "worker could not acquire independent scratch");
                });
            // Ordinary wait forces this contention case onto the actual worker.
            contended.wait();
            const job_handle occupied = scheduler.submit(
                [&]
                {
                    entered.store(true, std::memory_order_release);
                    wait_flag(release);
                });
            wait_flag(entered);
            render_graph_declaration_task task(scheduler);
            task.start(1, true, assisted);
            task.join();
            occupied.wait();
            require(ownerLock.owns_lock(), "assisted callback released the outer owner lock");
        }
        require(!thread_pool::is_worker_thread(), "assisting leaked worker status to the owner");
        {
            render_graph_declaration_lock again(scratch);
            render_graph_declaration_lock separate(independent);
            require(again.owns_lock() && separate.owns_lock(), "scope exit did not release locks and restore TLS");
        }
        try
        {
            render_graph_declaration_lock unwinding(scratch);
            require(unwinding.owns_lock(), "scratch was not available before owner unwind");
            throw std::runtime_error("lock owner failure");
        }
        catch (const std::runtime_error& error)
        {
            require(std::string_view(error.what()) == "lock owner failure", "unexpected lock owner error");
        }
        {
            render_graph_declaration_lock afterUnwind(scratch);
            require(afterUnwind.owns_lock(), "owner unwind did not restore the active lock chain");
        }
        scheduler.shutdown();
        std::cout << "PASS owner assistance: occupied worker, nested inline, worker status restored\n"
                     "PASS scratch lock: owner reentry, independent nesting, cross-worker contention, unwind restore\n";
    }
} // namespace

int main()
{
    try
    {
        thread_pool pool;
        job_scheduler scheduler(pool);
        unsigned fallback{};
        const auto stopped = [&](std::size_t) { ++fallback; };
        {
            render_graph_declaration_task task(scheduler);
            task.start(7, true, stopped);
            task.join();
        }
        require(fallback == 7, "stopped scheduler did not execute inline");
        exercise(scheduler, 1);
        exercise(scheduler, 4);
        exercise_owner_assist(scheduler);
        std::cout << "PASS stopped scheduler inline; production helper, JobScheduler.cpp and ThreadPool.cpp\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
