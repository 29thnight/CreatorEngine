#include "RHI/RHICommandRecordingJobs.h"
#include <iostream>
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
#include "ThreadPoolSpin.h"
#endif

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
    std::atomic<uint64_t> g_spinClockNanoseconds{1000000};

    uint64_t ReadSpinClock() noexcept
    {
        return g_spinClockNanoseconds.load(std::memory_order_relaxed);
    }

    void VerifySpinPolicy()
    {
        static_assert(!std::is_copy_constructible_v<job_spin_scope>);
        static_assert(std::is_nothrow_move_constructible_v<job_spin_scope>);
        static_assert(std::is_nothrow_move_assignable_v<job_spin_scope>);
        thread_pool_spin_control control{6, &ReadSpinClock};
        const auto hooks = control.callbacks();
        job_spin_options options;
        options.frame_id = 17;
        options.idle_budget = std::chrono::microseconds{25};
        options.frame_budget = std::chrono::microseconds{100};
        options.max_workers = 2;
        auto disabledOptions = options;
        disabledOptions.idle_budget = std::chrono::microseconds::zero();
        auto disabled = control.begin_scope(disabledOptions);
        Require(!disabled && !hooks.begin(hooks.userData, 2), "zero-budget spin scope admitted a worker");

        auto outer = control.begin_scope(options);
        auto nested = control.begin_scope(options);
        Require(outer && nested, "matching nested recording spin scopes were rejected");
        auto changedOptions = options;
        changedOptions.frame_budget = std::chrono::microseconds{200};
        Require(!control.begin_scope(changedOptions), "nested recording scope increased its frame spin budget");
        auto nextOptions = options;
        ++nextOptions.frame_id;
        Require(!control.begin_scope(nextOptions), "a new frame replaced an open recording spin scope");
        Require(!hooks.begin(hooks.userData, 6), "spin controller accepted an out-of-range worker slot");
        Require(hooks.begin(hooks.userData, 2) && hooks.begin(hooks.userData, 3),
                "recording spin policy did not admit its allowed workers");
        Require(!hooks.begin(hooks.userData, 4), "recording spin policy exceeded its simultaneous worker limit");
        Require(hooks.poll(hooks.userData, 2) == enki::IdleSpinDecision::Continue,
                "recording spin worker stopped before its injected deadline");
        nested.close();
        Require(hooks.poll(hooks.userData, 2) == enki::IdleSpinDecision::Continue,
                "closing a nested scope canceled its still-open outer scope");
        auto moved = std::move(outer);
        Require(!outer && moved, "moving a spin scope duplicated or dropped its ownership");

        // 실제 시계나 OS 스케줄링을 기다리지 않고 주입한 시각으로 예산 경계만 검증한다.
        g_spinClockNanoseconds.fetch_add(25000, std::memory_order_relaxed);
        Require(hooks.poll(hooks.userData, 2) == enki::IdleSpinDecision::BudgetExhausted &&
                    hooks.poll(hooks.userData, 3) == enki::IdleSpinDecision::BudgetExhausted,
                "recording spin continued past its per-idle deadline");
        hooks.end(hooks.userData, 2, enki::IdleSpinResult::BudgetExhausted);
        hooks.end(hooks.userData, 3, enki::IdleSpinResult::BudgetExhausted);
        Require(hooks.begin(hooks.userData, 4) && hooks.begin(hooks.userData, 5),
                "recording spin did not release finished worker slots");
        g_spinClockNanoseconds.fetch_add(5000, std::memory_order_relaxed);
        hooks.end(hooks.userData, 4, enki::IdleSpinResult::TaskFound);
        moved.close();
        auto reopened = control.begin_scope(options);
        Require(reopened && !hooks.begin(hooks.userData, 2),
                "reopening a recording scope refilled its consumed frame budget");
        Require(hooks.poll(hooks.userData, 5) == enki::IdleSpinDecision::ScopeEnded,
                "an old spinner continued into a reopened scope epoch");
        reopened.close();
        Require(!control.begin_scope(nextOptions), "a new frame replaced a spinner still leaving the previous epoch");
        hooks.end(hooks.userData, 5, enki::IdleSpinResult::ScopeEnded);
        const auto completed = moved.statistics();
        Require(completed.frame_id == options.frame_id && completed.spin_attempts == 4 &&
                    completed.reserved_budget_ns == 100000 && completed.active_spinners == 0 &&
                    completed.max_active_spinners == 2,
                "recording spin frame budget or worker-slot accounting changed");
        Require(completed.task_hits == 1 && completed.budget_exhaustions == 2 && completed.scope_exits == 1 &&
                    completed.shutdown_exits == 0,
                "recording spin exit reasons were lost or counted twice");

        auto next = control.begin_scope(nextOptions);
        Require(next && next.statistics().reserved_budget_ns == 0,
                "a fully retired frame prevented a fresh recording budget");
        outer.reset();
        moved.reset();
        reopened.reset();
        Require(hooks.begin(hooks.userData, 3) &&
                    hooks.poll(hooks.userData, 3) == enki::IdleSpinDecision::Continue,
                "resetting old scope tokens canceled the current frame");
        control.stop();
        Require(hooks.poll(hooks.userData, 3) == enki::IdleSpinDecision::Shutdown,
                "spin policy ignored scheduler shutdown");
        hooks.end(hooks.userData, 3, enki::IdleSpinResult::Shutdown);
        next.close();
        Require(next.statistics().shutdown_exits == 1 && next.statistics().active_spinners == 0 &&
                    !hooks.begin(hooks.userData, 2) && !control.begin_scope(nextOptions),
                "stopped spin policy retained a worker or admitted a new scope");
        Require(moved.statistics().frame_id == options.frame_id &&
                    moved.statistics().reserved_budget_ns == completed.reserved_budget_ns,
                "closed spin statistics were overwritten by a later frame");

        thread_pool_spin_control partial{4, &ReadSpinClock};
        const auto partialHooks = partial.callbacks();
        auto partialOptions = options;
        partialOptions.frame_budget = std::chrono::microseconds{35};
        partialOptions.max_workers = 1;
        auto partialScope = partial.begin_scope(partialOptions);
        Require(partialScope && partialHooks.begin(partialHooks.userData, 2),
                "partial-budget spin scope failed to start");
        partialHooks.end(partialHooks.userData, 2, enki::IdleSpinResult::TaskFound);
        Require(partialHooks.begin(partialHooks.userData, 2), "remaining frame budget was discarded");
        g_spinClockNanoseconds.fetch_add(9999, std::memory_order_relaxed);
        Require(partialHooks.poll(partialHooks.userData, 2) == enki::IdleSpinDecision::Continue,
                "partial spin allowance ended before its injected deadline");
        g_spinClockNanoseconds.fetch_add(1, std::memory_order_relaxed);
        Require(partialHooks.poll(partialHooks.userData, 2) == enki::IdleSpinDecision::BudgetExhausted,
                "partial spin allowance exceeded the remaining frame budget");
        partialHooks.end(partialHooks.userData, 2, enki::IdleSpinResult::BudgetExhausted);
        Require(!partialHooks.begin(partialHooks.userData, 2) &&
                    partialScope.statistics().reserved_budget_ns == 35000,
                "an early task hit refunded and reused its reserved spin allowance");
        partialScope.close();

        job_spin_scope retained;
        {
            thread_pool_spin_control temporary{4, &ReadSpinClock};
            retained = temporary.begin_scope(options);
        }
        retained.reset();
        Require(retained.statistics().frame_id == options.frame_id && retained.statistics().active_spinners == 0,
                "spin scope outliving its pool lost safe access to final statistics");
    }

    void VerifyTargetSerialization(job_scheduler& scheduler, const RHIRecordingExecutionOptions& options)
    {
        const auto graph = Build({0, 1, 0, 1, 0, 1}, {0, 0, 1, 1, 2, 2}, {0, 1, 2, 3, 4, 5}, {},
                                 std::vector<RHIRecordingPassDependencies>(6));
        std::array<std::atomic<uint32_t>, 2> active{};
        std::array<std::atomic<uint32_t>, 2> nextWave{};
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
        Require(active[0] == 0 && active[1] == 0 && nextWave[0] == 3 && nextWave[1] == 3,
                "target serialization omitted work or retained an active callback");
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
        handle.wait(std::chrono::microseconds{25});
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
        empty.wait(std::chrono::microseconds{-25});
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
            handle.wait(std::chrono::microseconds{25});
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
        auto success = scheduler.submit_graph(
            std::array{job_graph_node{}}, [capture = std::move(successCapture)](size_t) { (void)capture; });
        success.wait(std::chrono::microseconds{25});
        Require(successRetained.expired(), "successful native graph retained shared callback captures");
    }

    void VerifyBoundedOwnerWait(job_scheduler& scheduler)
    {
        for (const auto budget : {std::chrono::microseconds{0}, std::chrono::microseconds{25}})
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
                    throw std::runtime_error("terminal observer failure");
                });
            const auto handle = scheduler.submit(std::move(group));
            GateRelease cleanup{release};
            const bool observerStarted = entered.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
            const bool pending = !handle.is_complete();
            release.Open();
            bool failed = false;
            try
            {
                handle.wait(budget);
            }
            catch (const std::runtime_error& error)
            {
                failed = std::string_view{error.what()} == "terminal observer failure";
            }
            Require(observerStarted && pending, "completion became ready while a terminal observer was running");
            Require(failed && bodyRetained.expired() && observerRetained.expired(),
                    "bounded wait lost an observer error or returned before capture destruction");
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
                        child.wait(std::chrono::microseconds{25});
                    }
                    catch (const std::logic_error&)
                    {
                        rejected = true;
                    }
                    release.set_value();
                })
            .wait();
        child.wait();
        Require(rejected, "bounded wait accepted an incomplete nested worker wait");
        const auto completed = scheduler.submit([] {});
        completed.wait(std::chrono::microseconds{-25});
        scheduler.submit([completed] { completed.wait(std::chrono::microseconds{25}); }).wait();
        Require(completed.is_complete(), "completed worker wait or negative owner spin budget changed semantics");
    }

    void VerifyNativeShutdown(thread_pool& pool, job_scheduler& scheduler)
    {
        job_spin_options spinOptions;
        spinOptions.frame_id = 42;
        spinOptions.idle_budget = std::chrono::microseconds{25};
        spinOptions.frame_budget = std::chrono::microseconds{100};
        spinOptions.max_workers = 2;
        auto spinScope = scheduler.recording_spin_scope(spinOptions);
        std::vector<job_graph_node> nodes(2);
        nodes[1].dependencies = {0};
        ManualGate release;
        std::promise<void> rootEntered;
        auto entered = rootEntered.get_future();
        std::atomic<uint32_t> calls{0};
        auto handle = scheduler.submit_graph(
            nodes,
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
        auto observer = std::async(
            std::launch::async,
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
        handle.wait(std::chrono::microseconds{25});
        Require(rootStarted && admissionClosed && rejected && calls == 2,
                "shutdown did not stop admission and drain accepted native dependencies");
        Require(handle.is_complete() && !pool.is_running(), "native graph shutdown left pending work or workers");
        spinScope.close();
        const auto spinStatistics = spinScope.statistics();
        Require(spinStatistics.frame_id == spinOptions.frame_id && spinStatistics.active_spinners == 0 &&
                    spinStatistics.max_active_spinners <= 2 && spinStatistics.reserved_budget_ns <= 100000 &&
                    !scheduler.recording_spin_scope(spinOptions),
                "pool shutdown left an active recording spinner or allowed a new spin scope");
    }
#endif
} // namespace

int main()
{
    try
    {
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
        VerifyPlanning();
        VerifySpinPolicy();
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
            for (const bool batchSubmit : {false, true})
            {
                RHIRecordingExecutionOptions options;
                options.batchSubmit = batchSubmit;
                options.ownerSpinBudget = std::chrono::microseconds{25};
                VerifyExecution(scheduler, options);
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
                VerifyTargetSerialization(scheduler, options);
                VerifySplitProducerExecution(scheduler, options);
#endif
                if (workers == 4)
                {
                    VerifyNoGlobalBarrier(scheduler, options);
                }
            }
#if !defined(RECORDING_WAVE_JOIN_MUTANT)
            VerifyNativeSubmission(scheduler);
            VerifyNativeValidation(scheduler);
            VerifyNativeFailureAndLifetime(scheduler);
            VerifyBoundedOwnerWait(scheduler);
            VerifyNativeShutdown(pool, scheduler);
#else
            scheduler.shutdown();
#endif
        }
        std::cout << "RECORDING_TASK_GRAPH_OK checks=" << g_checks.load()
                  << " workers=1,4 policies=legacy,native bounded-wait=0,25us spin-policy=scope,budget,shutdown\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RECORDING_TASK_GRAPH_FAIL " << error.what() << '\n';
        return 1;
    }
}
