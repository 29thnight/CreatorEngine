#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Concurrency.JobGraph.h"
#else
import Creator.Concurrency.JobGraph;
#endif
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
                                std::vector<uint64_t> work, std::vector<RHIRecordingPassDependencies> passes,
                                RHIRecordingTaskTopology* topology = nullptr)
    {
        RHIRecordingTaskGraph graph;
        Require(RHIBuildRecordingTaskGraph(targets, waves, units, work, passes, 4, graph, topology),
                "graph build failed");
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
            // 잘못된 전체 wave 장벽만 주입하고 native DAG 입력 형식은 유효하게 유지한다.
            auto& dependencies = graph.nodes[node].dependencies;
            std::ranges::sort(dependencies);
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
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
        if (RenderContainerReuseEnabled())
        {
            const std::array targets{0u, 1u, 0u};
            const std::array waves{0u, 0u, 1u};
            const std::array units{0u, 1u, 2u};
            const std::array passes{RHIRecordingPassDependencies{false, {}}, RHIRecordingPassDependencies{false, {}},
                                    RHIRecordingPassDependencies{false, {0}}};
            RHIRecordingTaskGraph reusable;
            Require(RHIBuildRecordingTaskGraph(targets, waves, units, {}, passes, 4, reusable), "reuse seed failed");
            const auto* previous = reusable.scratch.previous.data();
            const auto* frontier = reusable.scratch.frontiers[0].data();
            const auto* dependency = reusable.nodes[2].dependencies.data();
            for (unsigned frame = 0; frame < 32; ++frame)
            {
                Require(RHIBuildRecordingTaskGraph(targets, waves, units, {}, passes, 4, reusable),
                        "reuse build failed");
                Require(reusable.scratch.previous.data() == previous &&
                            reusable.scratch.frontiers[0].data() == frontier &&
                            reusable.nodes[2].dependencies.data() == dependency &&
                            reusable.nodes[2].dependencies == std::vector<uint32_t>{0},
                        "recording scratch or nested dependencies were reallocated or accumulated");
            }
        }
        RHIRecordingTaskGraph invalid;
        Require(!RHIBuildRecordingTaskGraph(std::array{4u}, std::array{0u}, std::array{0u}, {},
                                            std::array{RHIRecordingPassDependencies{}}, 4, invalid),
                "invalid target accepted");
    }

    void VerifyNoGlobalBarrier(job_scheduler& scheduler, const RHIRecordingExecutionOptions& options)
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
                                       },
                                       options);
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

    void VerifyExecution(job_scheduler& scheduler, const RHIRecordingExecutionOptions& options)
    {
        auto graph = Build({0, 0, 1}, {0, 1, 0}, {0, 1, 2}, {}, {{false, {}}, {false, {0}}, {false, {}}});
        std::atomic<uint32_t> calls{0};
        bool failed = false;
        try
        {
            RHIRunCommandRecordingTaskGraph(
                scheduler, graph,
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
                },
                options);
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
                        RHIRunCommandRecordingTaskGraph(scheduler, graph, [](uint32_t) {}, options);
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
            RHIRunCommandRecordingTaskGraph(scheduler, graph, [&](uint32_t) { ++calls; }, options);
        }
        catch (const std::invalid_argument&)
        {
            failed = true;
        }
        Require(failed && calls == 2, "invalid DAG submitted partial work");
    }

#if !defined(RECORDING_WAVE_JOIN_MUTANT)
    void RequireSameExecutionGraph(const RHIRecordingTaskGraph& actual, const RHIRecordingTaskGraph& expected)
    {
        Require(actual.nodes.size() == expected.nodes.size(), "topology capture changed the execution node count");
        for (size_t node = 0; node < actual.nodes.size(); ++node)
        {
            const auto& left = actual.nodes[node];
            const auto& right = expected.nodes[node];
            Require(left.target == right.target && left.wave == right.wave && left.work == right.work &&
                        left.dependencies == right.dependencies,
                    "topology capture changed execution work, order, or dependencies");
        }
    }

    void RequireCompleteTopology(const RHIRecordingTaskGraph& graph, const RHIRecordingTaskTopology& topology)
    {
        Require(topology.available && !topology.truncated && topology.nodes.size() == graph.nodes.size(),
                "available topology omitted execution nodes");
        std::vector<uint32_t> seen(topology.unitNodes.size());
        for (size_t node = 0; node < graph.nodes.size(); ++node)
        {
            std::vector<uint32_t> dependencies;
            for (const auto& dependency : topology.nodes[node].dependencies)
            {
                dependencies.push_back(dependency.node);
                Require(dependency.reasons >= 1 && dependency.reasons <= 3,
                        "topology published an absent or unknown dependency reason");
            }
            Require(dependencies == graph.nodes[node].dependencies,
                    "topology did not describe every execution dependency exactly once");
            for (const auto unit : topology.nodes[node].units)
            {
                Require(unit < topology.unitNodes.size(), "topology unit index is out of range");
                Require(topology.unitNodes[unit] == node && ++seen[unit] == 1,
                        "topology unit ownership disagreed with its reverse mapping");
            }
        }
        Require(std::ranges::all_of(seen, [](uint32_t count) { return count == 1; }),
                "topology omitted a recording unit");
    }

    void RequireUnavailableTopology(const RHIRecordingTaskTopology& topology, bool truncated)
    {
        Require(!topology.available && topology.truncated == truncated && topology.nodes.empty() &&
                    topology.unitNodes.empty(),
                "unavailable topology exposed a partial or stale mapping");
    }

    void VerifyTopologyReasonsAndMapping()
    {
        constexpr auto kSameTarget = static_cast<uint8_t>(RHIRecordingDependencyReason::SameTarget);
        constexpr auto kCpuProducer = static_cast<uint8_t>(RHIRecordingDependencyReason::CpuProducer);
        RHIRecordingTaskTopology topology;
        auto graph =
            Build({0, 1, 0}, {0, 0, 1}, {0, 1, 2}, {1, 20, 1}, {{false, {}}, {false, {}}, {false, {0}}}, &topology);
        const auto withoutTopology =
            Build({0, 1, 0}, {0, 0, 1}, {0, 1, 2}, {1, 20, 1}, {{false, {}}, {false, {}}, {false, {0}}});
        RequireSameExecutionGraph(graph, withoutTopology);
        RequireCompleteTopology(graph, topology);
        const auto& combined = topology.nodes[2].dependencies;
        Require(combined.size() == 1 && combined[0].node == 0 && combined[0].reasons == (kSameTarget | kCpuProducer),
                "one execution edge lost or duplicated its overlapping dependency reasons");

        graph = Build({0, 1, 1}, {0, 0, 1}, {0, 1, 2}, {}, {{false, {}}, {true, {0}}, {false, {1}}}, &topology);
        RequireCompleteTopology(graph, topology);
        Require(topology.nodes.size() == 3, "independent pass topology changed its expected node count");
        const auto& bridged = topology.nodes[2].dependencies;
        Require(topology.nodes[1].dependencies.empty() && bridged.size() == 2 && bridged[0].node == 0 &&
                    bridged[0].reasons == kCpuProducer && bridged[1].node == 1 &&
                    bridged[1].reasons == (kSameTarget | kCpuProducer),
                "independent pass topology lost its inherited CPU frontier or blocked its own callback");

        // 입력 unit 순서와 target으로 정렬한 node 순서는 다르며, 같은 묶음의 두 unit도 각각 남아야 한다.
        graph = Build({1, 0, 2, 2}, {0, 0, 1, 1}, {0, 0, 1, 1}, {5, 7, 11, 13}, {{false, {}}, {false, {0}}}, &topology);
        RequireCompleteTopology(graph, topology);
        Require(topology.nodes.size() == 3, "split producer topology changed its expected node count");
        Require(topology.unitNodes == std::vector<uint32_t>({1, 0, 2, 2}) &&
                    topology.nodes[0].units == std::vector<uint32_t>{1} &&
                    topology.nodes[1].units == std::vector<uint32_t>{0} &&
                    topology.nodes[2].units == std::vector<uint32_t>({2, 3}),
                "topology confused original recording units with sorted graph nodes");
        Require(graph.nodes[0].work == 7 && graph.nodes[1].work == 5 && graph.nodes[2].work == 24,
                "topology capture changed coalesced work estimates");
        const auto& split = topology.nodes[2].dependencies;
        Require(split.size() == 2 && split[0].node == 0 && split[1].node == 1 && split[0].reasons == kCpuProducer &&
                    split[1].reasons == kCpuProducer,
                "split producer topology omitted a slice or invented a same-target reason");

        graph = Build({0, 0}, {0, 0}, {0, 1}, {}, {{false, {}}, {false, {0}}}, &topology);
        RequireCompleteTopology(graph, topology);
        Require(graph.nodes.size() == 1 && topology.nodes[0].dependencies.empty() &&
                    topology.nodes[0].units == std::vector<uint32_t>({0, 1}),
                "coalesced passes created a self dependency or lost a recording unit");
        const auto previous = graph;
        Require(!RHIBuildRecordingTaskGraph(
                    std::array{0u, 0u}, std::array{0u, 1u}, std::array{0u, 1u}, {},
                    std::array{RHIRecordingPassDependencies{}, RHIRecordingPassDependencies{false, {2}}}, 1, graph,
                    &topology),
                "invalid producer accepted after building partial topology");
        RequireUnavailableTopology(topology, false);
        RequireSameExecutionGraph(graph, previous);
        Require(RHIBuildRecordingTaskGraph({}, {}, {}, {}, {}, 1, graph, &topology),
                "empty recording graph was rejected");
        RequireCompleteTopology(graph, topology);
        Require(graph.nodes.empty() && topology.unitNodes.empty(), "empty topology retained previous units");
    }

    void VerifyTopologyEntryLimit()
    {
        constexpr auto kMaximumEntries = static_cast<uint32_t>(RHIRecordingTaskTopology::kMaximumEntries);
        std::vector<uint32_t> targets(kMaximumEntries), waves(kMaximumEntries), unitPasses(kMaximumEntries);
        for (uint32_t unit = 0; unit < kMaximumEntries; ++unit)
        {
            targets[unit] = unit;
        }
        const std::array passes{RHIRecordingPassDependencies{}};
        RHIRecordingTaskGraph graph, withoutTopology;
        RHIRecordingTaskTopology topology;
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, kMaximumEntries, graph, &topology),
                "recording topology rejected its exact entry limit");
        Require(topology.available && !topology.truncated && topology.nodes.size() == kMaximumEntries &&
                    topology.unitNodes == targets && graph.nodes.size() == kMaximumEntries,
                "recording topology truncated at rather than above its entry limit");
        targets.push_back(kMaximumEntries);
        waves.push_back(0);
        unitPasses.push_back(0);
        Require(
            RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, kMaximumEntries + 1, graph, &topology),
            "diagnostic entry cap rejected valid execution work");
        RequireUnavailableTopology(topology, true);
        Require(
            RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, kMaximumEntries + 1, withoutTopology),
            "entry-cap comparison graph was rejected");
        RequireSameExecutionGraph(graph, withoutTopology);

        // node가 한 개로 합쳐져도 원래 unit의 수가 상한을 넘으면 역매핑 일부를 공개하면 안 된다.
        std::ranges::fill(targets, 0u);
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, graph, &topology),
                "unit cap rejected a valid coalesced execution graph");
        RequireUnavailableTopology(topology, true);
        Require(graph.nodes.size() == 1 && graph.nodes[0].work == kMaximumEntries + 1,
                "unit cap omitted execution work while suppressing topology");
        const auto previous = graph;
        unitPasses.back() = 1;
        Require(!RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, graph, &topology),
                "topology truncation bypassed execution input validation");
        RequireUnavailableTopology(topology, false);
        RequireSameExecutionGraph(graph, previous);
    }

    void VerifyTopologyDependencyLimit()
    {
        constexpr uint32_t kNodes = 182;
        // 앞의 181개 node 완전 DAG는 16290개 간선이다. 마지막 94개를 더하면 정확히 16384개다.
        constexpr uint32_t kLastProducerPrefix = 93;
        static_assert((kNodes - 1) * (kNodes - 2) / 2 + kLastProducerPrefix + 1 ==
                      RHIRecordingTaskTopology::kMaximumDependencies);
        std::vector<uint32_t> targets(kNodes), waves(kNodes), unitPasses(kNodes);
        std::vector<RHIRecordingPassDependencies> passes(kNodes);
        for (uint32_t node = 0; node < kNodes; ++node)
        {
            waves[node] = node;
            unitPasses[node] = node;
            const auto producerCount = node + 1 == kNodes ? kLastProducerPrefix : node;
            for (uint32_t producer = 0; producer < producerCount; ++producer)
            {
                passes[node].producers.push_back(producer);
            }
        }
        // 마지막 같은-target 간선은 CPU 간선과 겹친다. cap을 채운 뒤에도 중복 이유는 병합해야 한다.
        passes.back().producers.push_back(kNodes - 2);
        RHIRecordingTaskGraph graph, withoutTopology;
        RHIRecordingTaskTopology topology;
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, graph, &topology),
                "recording topology rejected its exact unique-dependency limit");
        RequireCompleteTopology(graph, topology);
        size_t dependencies = 0;
        for (const auto& node : topology.nodes)
        {
            dependencies += node.dependencies.size();
        }
        Require(dependencies == RHIRecordingTaskTopology::kMaximumDependencies && !topology.nodes.empty() &&
                    !topology.nodes.back().dependencies.empty(),
                "exact dependency cap omitted topology edges");
        const auto& last = topology.nodes.back().dependencies.back();
        Require(last.node == kNodes - 2 &&
                    last.reasons == (static_cast<uint8_t>(RHIRecordingDependencyReason::SameTarget) |
                                     static_cast<uint8_t>(RHIRecordingDependencyReason::CpuProducer)),
                "dependency cap counted duplicate reasons as separate execution edges");
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, withoutTopology),
                "exact dependency-limit comparison graph was rejected");
        RequireSameExecutionGraph(graph, withoutTopology);

        passes.back().producers.push_back(kLastProducerPrefix);
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, graph, &topology),
                "diagnostic dependency cap rejected a valid execution graph");
        RequireUnavailableTopology(topology, true);
        Require(RHIBuildRecordingTaskGraph(targets, waves, unitPasses, {}, passes, 1, withoutTopology),
                "over dependency-limit comparison graph was rejected");
        RequireSameExecutionGraph(graph, withoutTopology);
        dependencies = 0;
        for (const auto& node : graph.nodes)
        {
            dependencies += node.dependencies.size();
        }
        Require(dependencies == RHIRecordingTaskTopology::kMaximumDependencies + 1,
                "topology truncation dropped a required execution dependency");
    }

    void VerifyTargetSerialization(job_scheduler& scheduler, const RHIRecordingExecutionOptions& options)
    {
        constexpr uint32_t kTargets = 8;
        constexpr uint32_t kWaves = 3;
        std::array<uint32_t, kTargets * kWaves> targets{}, waves{}, units{};
        std::array<RHIRecordingPassDependencies, kTargets * kWaves> passes{};
        for (uint32_t unit = 0; unit < targets.size(); ++unit)
        {
            targets[unit] = unit % kTargets;
            waves[unit] = unit / kTargets;
            units[unit] = unit;
        }
        RHIRecordingTaskGraph graph;
        Require(RHIBuildRecordingTaskGraph(targets, waves, units, {}, passes, kTargets, graph),
                "maximum-target graph build failed");
        std::array<std::atomic<uint32_t>, kTargets> active{};
        std::array<std::atomic<uint32_t>, kTargets> nextWave{};
        RHIRunCommandRecordingTaskGraph(
            scheduler, graph,
            [&](uint32_t node)
            {
                const auto& task = graph.nodes[node];
                const auto previousActive = active[task.target].fetch_add(1);
                const auto expectedWave = nextWave[task.target].fetch_add(1);
                // 실행 순서를 바꾸어도 같은 command target은 겹치지 않아야 한다.
                for (uint32_t attempt = 0; attempt < 32; ++attempt)
                {
                    std::this_thread::yield();
                }
                active[task.target].fetch_sub(1);
                Require(previousActive == 0, "recording callbacks overlapped on the same command target");
                Require(expectedWave == task.wave, "recording target sequence changed");
            },
            options);
        for (uint32_t target = 0; target < kTargets; ++target)
        {
            Require(active[target] == 0 && nextWave[target] == kWaves,
                    "target serialization omitted work or retained an active callback");
        }
    }

    void VerifySplitProducerExecution(job_scheduler& scheduler, const RHIRecordingExecutionOptions& options)
    {
        const auto graph = Build({0, 1, 2}, {0, 0, 1}, {0, 0, 1}, {}, {{false, {}}, {false, {0}}});
        std::array<std::atomic<uint32_t>, 2> published{};
        std::atomic<uint32_t> consumers{0};
        RHIRunCommandRecordingTaskGraph(
            scheduler, graph,
            [&](uint32_t node)
            {
                if (node < 2)
                {
                    published[node].store(node + 1, std::memory_order_release);
                    return;
                }
                Require(published[0].load(std::memory_order_acquire) == 1 &&
                            published[1].load(std::memory_order_acquire) == 2,
                        "recording consumer missed a split producer");
                ++consumers;
            },
            options);
        Require(consumers == 1, "split producer consumer was skipped or repeated");
    }

    class ManualGate
    {
      public:
        void Wait()
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return open_; });
        }

        void Open()
        {
            {
                std::lock_guard lock(mutex_);
                open_ = true;
            }
            ready_.notify_all();
        }

      private:
        std::mutex mutex_;
        std::condition_variable ready_;
        bool open_{false};
    };

    class GateRelease
    {
      public:
        explicit GateRelease(ManualGate& gate) : gate_(gate) {}
        ~GateRelease() { gate_.Open(); }
        GateRelease(const GateRelease&) = delete;
        GateRelease& operator=(const GateRelease&) = delete;

      private:
        ManualGate& gate_;
    };

    void VerifyNativeSubmission(job_scheduler& scheduler)
    {
        std::vector<job_graph_node> nodes(5);
        nodes[1].dependencies = {0};
        nodes[2].dependencies = {0};
        nodes[3].dependencies = {1, 2};
        nodes[4].dependencies = {0};
        std::array<std::atomic<uint32_t>, 5> calls{};
        const auto submitter = std::this_thread::get_id();
        auto diagnostics = std::make_shared<job_graph_diagnostics>();
        auto handle = scheduler.submit_graph(
            nodes,
            [&](size_t node)
            {
                Require(std::this_thread::get_id() != submitter && thread_pool::is_worker_thread(),
                        "native graph executed user work on its submitting thread");
                for (const auto dependency : nodes[node].dependencies)
                {
                    Require(calls[dependency].load(std::memory_order_acquire) == 1,
                            "native graph violated a prerequisite");
                }
                calls[node].fetch_add(1, std::memory_order_release);
            },
            diagnostics);
        handle.wait();
        Require(handle.is_complete() && diagnostics->nodes.size() == nodes.size(),
                "native graph did not publish complete diagnostics");
        Require(diagnostics->dispatcher_handoffs == 1 && diagnostics->native_task_count == nodes.size(),
                "native graph expanded back into per-node dispatcher handoffs");
        Require(diagnostics->submitted_ns != 0 && diagnostics->dispatcher_begin_ns >= diagnostics->submitted_ns &&
                    diagnostics->completed_ns >= diagnostics->dispatcher_begin_ns,
                "native graph aggregate timestamps are incomplete or reversed");
        for (size_t node = 0; node < nodes.size(); ++node)
        {
            Require(calls[node] == 1, "native graph callback was omitted or repeated");
            const auto& timing = diagnostics->nodes[node];
            Require(!timing.skipped && timing.registered_ns != 0 && timing.ready_ns >= timing.registered_ns &&
                        timing.publish_begin_ns >= timing.ready_ns && timing.start_ns >= timing.publish_begin_ns &&
                        timing.body_end_ns >= timing.start_ns && timing.complete_ns >= timing.body_end_ns &&
                        diagnostics->completed_ns >= timing.complete_ns,
                    "native graph node timestamps are incomplete or reversed");
            // 큐가 가득 차면 게시 도중 작업이 실행될 수 있으므로 publish_end와 start의 순서는 강제하지 않는다.
            Require(timing.publish_end_ns >= timing.publish_begin_ns,
                    "native graph did not close its publication interval");
            for (const auto dependency : nodes[node].dependencies)
            {
                Require(timing.ready_ns >= diagnostics->nodes[dependency].complete_ns,
                        "dependent readiness preceded prerequisite completion");
            }
        }
    }

    void VerifyNativeValidation(job_scheduler& scheduler)
    {
        std::atomic<uint32_t> calls{0};
        std::vector<std::vector<job_graph_node>> invalid(4, std::vector<job_graph_node>(2));
        invalid[0][0].dependencies = {0};
        invalid[1][0].dependencies = {1};
        invalid[2][1].dependencies = {0, 0};
        invalid[3][1].dependencies = {UINT32_MAX};
        for (const auto& nodes : invalid)
        {
            bool rejected = false;
            job_handle unexpected;
            try
            {
                unexpected = scheduler.submit_graph(nodes, [&](size_t) { ++calls; });
            }
            catch (const std::invalid_argument&)
            {
                rejected = true;
            }
            if (unexpected.valid())
            {
                unexpected.wait();
            }
            Require(rejected && calls == 0, "invalid native graph admitted partial work");
        }
        bool rejected = false;
        try
        {
            scheduler.submit_graph(std::array{job_graph_node{}}, {});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        Require(rejected, "nonempty native graph accepted an empty callback");
        const auto empty = scheduler.submit_graph({}, {});
        empty.wait();
        Require(empty.valid() && empty.is_complete(), "empty native graph did not complete");
    }

    void VerifyNativeFailureAndLifetime(job_scheduler& scheduler)
    {
        std::vector<job_graph_node> nodes(3);
        nodes[2].dependencies = {0};
        std::array<std::atomic<uint32_t>, 3> calls{};
        auto capture = std::make_shared<uint32_t>(42);
        std::weak_ptr<uint32_t> retained = capture;
        ManualGate release;
        std::promise<void> independentEntered;
        auto entered = independentEntered.get_future();
        auto diagnostics = std::make_shared<job_graph_diagnostics>();
        auto handle = scheduler.submit_graph(
            nodes,
            [&, capture = std::move(capture)](size_t node)
            {
                ++calls[node];
                if (node == 0)
                {
                    throw std::runtime_error("native recording failure");
                }
                if (node == 1)
                {
                    independentEntered.set_value();
                    release.Wait();
                    Require(*capture == 42, "native graph released its shared capture while work was running");
                }
            },
            diagnostics);
        GateRelease cleanup{release};
        // 이 제한은 교착된 회귀 실행을 끝내기 위한 watchdog이며 성능 판정값이 아니다.
        const bool independentStarted = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        const bool pending = !handle.is_complete();
        release.Open();
        bool failed = false;
        try
        {
            handle.wait();
        }
        catch (const std::runtime_error& error)
        {
            failed = std::string_view{error.what()} == "native recording failure";
        }
        Require(independentStarted && pending, "native graph completed before independent work was drained");
        Require(failed && calls[0] == 1 && calls[1] == 1 && calls[2] == 0,
                "native failure lost its error, skipped independent work, or executed a failed dependent");
        Require(retained.expired(), "completed native graph handle retained callback captures");
        Require(diagnostics->nodes.size() == 3 && diagnostics->nodes[2].skipped &&
                    diagnostics->completed_ns >= diagnostics->nodes[1].complete_ns,
                "failed native graph did not publish skipped-node and drain diagnostics");

        auto successCapture = std::make_shared<uint32_t>(7);
        std::weak_ptr<uint32_t> successRetained = successCapture;
        auto success = scheduler.submit_graph(std::array{job_graph_node{}},
                                              [capture = std::move(successCapture)](size_t) { (void)capture; });
        success.wait();
        Require(successRetained.expired(), "successful native graph retained shared callback captures");
    }

    void VerifyNativeStorageReuse(job_scheduler& scheduler)
    {
        std::array<job_graph_node, 4> nodes;
        nodes[1].dependencies = {0};
        nodes[2].dependencies = {1};
        auto diagnostics = std::make_shared<job_graph_diagnostics>();
        for (size_t iteration = 0; iteration < 128; ++iteration)
        {
            std::array<std::atomic<uint32_t>, 4> calls{};
            auto capture = std::make_shared<size_t>(iteration);
            std::weak_ptr<size_t> weak = capture;
            const bool fail = iteration % 4 == 0;
            auto handle = scheduler.submit_graph(
                nodes,
                [&, capture = std::move(capture)](size_t node)
                {
                    ++calls[node];
                    if (node == 0 && fail)
                    {
                        throw std::runtime_error("reused root failure");
                    }
                    Require(*capture == iteration, "Reused native graph read an earlier capture.");
                },
                diagnostics);
            bool failed{};
            try
            {
                handle.wait();
            }
            catch (const std::runtime_error& error)
            {
                failed = std::string_view(error.what()) == "reused root failure";
            }
            Require(failed == fail && calls[0] == 1 && calls[1] == (fail ? 0u : 1u) && calls[2] == (fail ? 0u : 1u) &&
                        calls[3] == 1,
                    "Native storage reuse kept a failure bit or changed dependency execution.");
            Require(weak.expired(), "Native storage cache retained a completed callback.");
            if (iteration)
            {
                Require(diagnostics->native_storage_reused, "Identical native graph allocated its storage again.");
            }
        }
    }

    void VerifyCompletionWait(job_scheduler& scheduler)
    {
        for (const bool observerFails : {false, true})
        {
            ManualGate release;
            std::promise<void> observerEntered;
            auto entered = observerEntered.get_future();
            auto bodyCapture = std::make_shared<uint32_t>(1);
            auto observerCapture = std::make_shared<uint32_t>(2);
            std::weak_ptr<uint32_t> bodyRetained = bodyCapture;
            std::weak_ptr<uint32_t> observerRetained = observerCapture;
            job_group group;
            group.add([capture = std::move(bodyCapture)] { (void)capture; });
            group.on_complete(
                [&, capture = std::move(observerCapture)](std::exception_ptr error)
                {
                    observerEntered.set_value();
                    release.Wait();
                    Require(!error && bodyRetained.expired() && *capture == 2,
                            "terminal observer ran before task captures were released");
                    if (observerFails)
                    {
                        throw std::runtime_error("terminal observer failure");
                    }
                });
            const auto handle = scheduler.submit(std::move(group));
            GateRelease cleanup{release};
            const bool observerStarted = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            const bool pending = !handle.is_complete();
            release.Open();
            bool failed = false;
            try
            {
                handle.wait();
            }
            catch (const std::runtime_error& error)
            {
                Require(std::string_view{error.what()} == "terminal observer failure",
                        "completion wait reported an unexpected observer error");
                failed = true;
            }
            Require(observerStarted && pending, "completion became ready while a terminal observer was running");
            Require(failed == observerFails && bodyRetained.expired() && observerRetained.expired(),
                    "completion wait lost an observer result or returned before capture destruction");
        }

        std::promise<void> release;
        const auto gate = release.get_future().share();
        std::atomic<bool> rejected{false};
        job_handle child;
        scheduler
            .submit(
                [&]
                {
                    child = scheduler.submit(
                        [gate]
                        {
                            // 여러 worker에서 잘못 허용된 대기는 gate 시간 초과 뒤 실패한다.
                            // 단일 worker 교착은 실행 스크립트의 별도 watchdog이 종료한다.
                            gate.wait_for(std::chrono::seconds(2));
                        });
                    try
                    {
                        child.wait();
                    }
                    catch (const std::logic_error&)
                    {
                        rejected = true;
                    }
                    release.set_value();
                })
            .wait();
        child.wait();
        Require(rejected, "completion wait accepted an incomplete nested worker wait");
        const auto completed = scheduler.submit([] {});
        completed.wait();
        scheduler.submit([completed] { completed.wait(); }).wait();
        Require(completed.is_complete(), "completed worker wait changed completion semantics");
    }

    void VerifyNativeShutdown(thread_pool& pool, job_scheduler& scheduler)
    {
        std::vector<job_graph_node> nodes(2);
        nodes[1].dependencies = {0};
        ManualGate release;
        std::promise<void> rootEntered;
        auto entered = rootEntered.get_future();
        std::atomic<uint32_t> calls{0};
        auto handle = scheduler.submit_graph(nodes,
                                             [&](size_t node)
                                             {
                                                 if (node == 0)
                                                 {
                                                     rootEntered.set_value();
                                                     release.Wait();
                                                 }
                                                 ++calls;
                                             });
        const bool rootStarted = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
        auto observer =
            std::async(std::launch::async,
                       [&]
                       {
                           GateRelease releaseOnExit{release};
                           const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                           while (scheduler.is_running() && std::chrono::steady_clock::now() < deadline)
                           {
                               std::this_thread::yield();
                           }
                           const bool admissionClosed = !scheduler.is_running();
                           bool rejected = false;
                           try
                           {
                               scheduler.submit_graph(std::array{job_graph_node{}}, [&](size_t) { ++calls; });
                           }
                           catch (const std::runtime_error&)
                           {
                               rejected = true;
                           }
                           return std::pair{admissionClosed, rejected};
                       });
        GateRelease cleanup{release};
        // enki 수명 API는 start와 같은 소유자에서 호출하고 관찰자만 별도 스레드로 둔다.
        scheduler.shutdown();
        const auto [admissionClosed, rejected] = observer.get();
        handle.wait();
        Require(rootStarted && admissionClosed && rejected && calls == 2,
                "shutdown did not stop admission and drain accepted native dependencies");
        Require(handle.is_complete() && !pool.is_running(), "native graph shutdown left pending work or workers");
    }
#endif
} // namespace

int main()
{
    try
    {
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
        VerifyPlanning();
        VerifyTopologyReasonsAndMapping();
        VerifyTopologyEntryLimit();
        VerifyTopologyDependencyLimit();
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
        for (const auto workers : {1u, 4u, 8u})
        {
            scheduler.start(workers);
            for (const bool batchSubmit : {false, true})
            {
                RHIRecordingExecutionOptions options;
                options.batchSubmit = batchSubmit;
                VerifyExecution(scheduler, options);
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
                VerifyTargetSerialization(scheduler, options);
                VerifySplitProducerExecution(scheduler, options);
#endif
                if (workers > 1)
                {
                    VerifyNoGlobalBarrier(scheduler, options);
                }
            }
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
            VerifyNativeSubmission(scheduler);
            VerifyNativeValidation(scheduler);
            VerifyNativeFailureAndLifetime(scheduler);
            VerifyNativeStorageReuse(scheduler);
            VerifyCompletionWait(scheduler);
            VerifyNativeShutdown(pool, scheduler);
#else
            scheduler.shutdown();
#endif
        }
        std::cout << "RECORDING_TASK_GRAPH_OK checks=" << g_checks.load()
                  << " workers=1,4,8 targets=8 policies=legacy,native wait=cv lifetime=callback,observer"
                  << " shutdown=drain topology=bounded\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RECORDING_TASK_GRAPH_FAIL " << error.what() << '\n';
        return 1;
    }
}
