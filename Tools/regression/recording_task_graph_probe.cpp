#include "RHI/RHICommandRecordingJobs.h"
#include <iostream>

namespace
{
    std::atomic<uint32_t> g_checks{0};

    void Require(bool condition, const char* message)
    {
        ++g_checks;
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    RHIRecordingTaskGraph Build(std::vector<uint32_t> targets, std::vector<uint32_t> waves, std::vector<uint32_t> units,
                                std::vector<uint64_t> work, std::vector<RHIRecordingPassDependencies> passes)
    {
        RHIRecordingTaskGraph graph;
        Require(RHIBuildRecordingTaskGraph(targets, waves, units, work, passes, 4, graph), "graph build failed");
#if defined(RECORDING_WAVE_JOIN_MUTANT)
        // 이전 전체 wave 합류를 되살리면 독립 후속 작업의 조기 실행 검사가 실패해야 한다.
        for (uint32_t node = 0; node < graph.nodes.size(); ++node)
        {
            for (uint32_t prior = 0; prior < node; ++prior)
            {
                if (graph.nodes[prior].wave < graph.nodes[node].wave)
                {
                    graph.nodes[node].dependencies.push_back(prior);
                }
            }
        }
#endif
        return graph;
    }

    void VerifyPlanning()
    {
        auto graph = Build({0, 1, 0}, {0, 0, 1}, {0, 1, 2}, {1, 20, 1}, {{false, {}}, {false, {}}, {false, {0}}});
        Require(graph.nodes.size() == 3 && graph.nodes[2].dependencies == std::vector<uint32_t>{0},
                "unrelated wave work became a dependency");
        Require(graph.CriticalWork() == 20 && graph.ReplacesWaveJoins(), "dependency critical path is incorrect");
        graph = Build({0, 1, 1}, {0, 0, 1}, {0, 1, 2}, {}, {{false, {}}, {true, {0}}, {false, {1}}});
        Require(graph.nodes[1].dependencies.empty() && graph.nodes[2].dependencies == std::vector<uint32_t>({0, 1}),
                "independent bridge lost its upstream CPU prerequisite");
        graph = Build({0, 1, 2}, {0, 0, 1}, {0, 0, 1}, {}, {{false, {}}, {false, {0}}});
        Require(graph.nodes[2].dependencies == std::vector<uint32_t>({0, 1}),
                "split producer did not join every slice");
        graph = Build({0, 0, 0}, {0, 1, 2}, {0, 1, 2}, {}, {{false, {}}, {false, {0}}, {false, {1}}});
        Require(!graph.ReplacesWaveJoins(), "single-target coalescing would be expanded into more jobs");
        RHIRecordingTaskGraph invalid;
        Require(!RHIBuildRecordingTaskGraph(std::array{4u}, std::array{0u}, std::array{0u}, {},
                                            std::array{RHIRecordingPassDependencies{}}, 4, invalid),
                "invalid target accepted");
    }

    void VerifyNoGlobalBarrier(job_scheduler& scheduler)
    {
        auto graph = Build({0, 1, 0}, {0, 0, 1}, {0, 1, 2}, {}, {{false, {}}, {false, {}}, {false, {0}}});
        std::promise<void> release;
        const auto gate = release.get_future().share();
        std::promise<void> advanced;
        const auto progress = advanced.get_future();
        std::array<std::atomic<uint32_t>, 3> calls{};
        std::atomic<bool> producerDone{false};
        auto host = std::async(std::launch::async,
                               [&]
                               {
                                   RHIRunCommandRecordingTaskGraph(
                                       scheduler, graph,
                                       [&](uint32_t node)
                                       {
                                           Require(thread_pool::is_worker_thread(), "caller executed recording work");
                                           ++calls[node];
                                           if (node == 0)
                                           {
                                               producerDone.store(true, std::memory_order_release);
                                           }
                                           else if (node == 1)
                                           {
                                               gate.wait();
                                           }
                                           else
                                           {
                                               Require(producerDone.load(std::memory_order_acquire),
                                                       "consumer ran before producer");
                                               advanced.set_value();
                                           }
                                       });
                               });
        const bool early = progress.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        release.set_value();
        host.get();
        Require(early, "consumer waited for unrelated work in the previous wave");
        for (const auto& count : calls)
        {
            Require(count == 1, "callback omitted or executed twice");
        }
    }

    void VerifyExecution(job_scheduler& scheduler)
    {
        auto graph = Build({0, 0, 1}, {0, 1, 0}, {0, 1, 2}, {}, {{false, {}}, {false, {0}}, {false, {}}});
        std::atomic<uint32_t> calls{0};
        bool failed = false;
        try
        {
            RHIRunCommandRecordingTaskGraph(scheduler, graph,
                                            [&](uint32_t node)
                                            {
                                                if (graph.nodes[node].target == 0)
                                                {
                                                    ++calls;
                                                    if (graph.nodes[node].wave == 0)
                                                    {
                                                        throw std::runtime_error("recording task failure");
                                                    }
                                                }
                                                else
                                                {
                                                    std::this_thread::sleep_for(std::chrono::milliseconds(3));
                                                    ++calls;
                                                }
                                            });
        }
        catch (const std::runtime_error& error)
        {
            failed = std::string_view{error.what()} == "recording task failure";
        }
        Require(failed && calls == 2, "failed dependency ran or independent work was not drained");
        bool nested = false;
        scheduler
            .submit(
                [&]
                {
                    try
                    {
                        RHIRunCommandRecordingTaskGraph(scheduler, graph, [](uint32_t) {});
                    }
                    catch (const std::logic_error&)
                    {
                        nested = true;
                    }
                })
            .wait();
        Require(nested, "nested recording wait was accepted");
        graph.nodes[0].dependencies.push_back(0);
        failed = false;
        try
        {
            RHIRunCommandRecordingTaskGraph(scheduler, graph, [&](uint32_t) { ++calls; });
        }
        catch (const std::invalid_argument&)
        {
            failed = true;
        }
        Require(failed && calls == 2, "invalid DAG submitted partial work");
    }
} // namespace

int main()
{
    try
    {
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
        VerifyPlanning();
#endif
        thread_pool pool;
        job_scheduler scheduler{pool};
        bool stopped = false;
        try
        {
            auto graph = Build({0}, {0}, {0}, {}, {{false, {}}});
            RHIRunCommandRecordingTaskGraph(scheduler, graph, [](uint32_t) {});
        }
        catch (const std::runtime_error&)
        {
            stopped = true;
        }
        Require(stopped, "stopped scheduler accepted recording work");
        for (const auto workers : {1u, 4u})
        {
            scheduler.start(workers);
            VerifyExecution(scheduler);
            if (workers == 4)
            {
                VerifyNoGlobalBarrier(scheduler);
            }
            scheduler.shutdown();
        }
        std::cout << "RECORDING_TASK_GRAPH_OK checks=" << g_checks.load() << " workers=1,4\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RECORDING_TASK_GRAPH_FAIL " << error.what() << '\n';
        return 1;
    }
}
