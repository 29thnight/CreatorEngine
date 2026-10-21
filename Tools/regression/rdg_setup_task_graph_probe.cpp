#include "Render/Graph/RGSetupTaskGraph.h"
#include <iostream>

namespace
{
    void Require(bool value)
    {
        if (!value)
        {
            throw std::runtime_error("RDG setup regression failed");
        }
    }
    template<class F>
    void Reject(F&& callback)
    {
        bool rejected{};
        try
        {
            callback();
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected);
    }
} // namespace
int main()
{
    try
    {
        for (const auto workers : {1u, 4u, 8u})
        {
            thread_pool pool;
            job_scheduler scheduler{pool};
            scheduler.start(workers);
            RGSetupTaskGraph graph{scheduler};
            RGSetupTaskGraph foreign{scheduler};
            std::atomic<unsigned> value{};
            const auto first = graph.AddTask(0, {}, RGSetupWaitPoint::Compile, [&] { value.fetch_add(1); });
            const std::array dependencies{first};
            const auto second = graph.AddTask(0, dependencies, RGSetupWaitPoint::Compile,
                                              [&]
                                              {
                                                  Require(value.load() == 1);
                                                  value.fetch_add(2);
                                              });
            graph.Wait(second);
            Require(value.load() == 3);
            Reject([&] { foreign.Wait(first); });
            graph.Reset();
            Reject([&] { graph.Wait(first); });
            const auto late = graph.AddTask(0, {}, RGSetupWaitPoint::Execute, [] {});
            Reject(
                [&]
                {
                    const std::array prerequisite{late};
                    graph.AddTask(0, prerequisite, RGSetupWaitPoint::Compile, [] {});
                });
            graph.Join(RGSetupWaitPoint::Execute);
            graph.Reset();
            const auto sealed = graph.AddTask(0, {}, RGSetupWaitPoint::Compile, [] {});
            graph.Join(RGSetupWaitPoint::Compile);
            Reject([&] { graph.AddTask(0, {}, RGSetupWaitPoint::Compile, [] {}); });
            const std::array sealedDependency{sealed};
            graph.AddTask(0, sealedDependency, RGSetupWaitPoint::Execute, [] {});
            graph.Join(RGSetupWaitPoint::Execute);
            Reject([&] { graph.AddTask(0, {}, RGSetupWaitPoint::Execute, [] {}); });
            graph.Reset();
            std::atomic<bool> descendant{}, independent{};
            const auto failure = graph.AddTask(0, {}, RGSetupWaitPoint::Compile,
                                               [] { throw std::runtime_error("injected setup failure"); });
            const std::array failed{failure};
            graph.AddTask(0, failed, RGSetupWaitPoint::Compile, [&] { descendant = true; });
            graph.AddTask(0, {}, RGSetupWaitPoint::Compile, [&] { independent = true; });
            bool propagated{};
            try
            {
                graph.Join(RGSetupWaitPoint::Compile);
            }
            catch (const std::runtime_error&)
            {
                propagated = true;
            }
            Require(propagated && !descendant && independent);
            graph.Reset();
            auto owner = std::make_shared<int>(5);
            std::weak_ptr<int> weak = owner;
            graph.AddTask(0, {}, RGSetupWaitPoint::Execute, [owner] { Require(*owner == 5); });
            owner.reset();
            graph.Join(RGSetupWaitPoint::Execute);
            Require(weak.expired());
            graph.Reset();
            if (workers > 1)
            {
                std::mutex mutex;
                std::condition_variable cv;
                unsigned entered{};
                const auto rendezvous = [&]
                {
                    std::unique_lock lock{mutex};
                    ++entered;
                    cv.notify_all();
                    Require(cv.wait_for(lock, std::chrono::seconds{2}, [&] { return entered == 2; }));
                };
                graph.AddTask(0, {}, RGSetupWaitPoint::Compile, rendezvous);
                graph.AddTask(0, {}, RGSetupWaitPoint::Compile, rendezvous);
                graph.Join(RGSetupWaitPoint::Compile);
                Require(entered == 2);
            }
            scheduler.shutdown();
        }
        std::cout << "RDG_SETUP_TASK_GRAPH_OK workers=1,4,8 order=dependency failure=drained lifetime=released "
                     "stale=rejected\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
