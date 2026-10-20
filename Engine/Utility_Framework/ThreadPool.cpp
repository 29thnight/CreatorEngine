#include "ThreadPool.h"
#include <enkiTS/TaskScheduler.h>
#include "StandardLibrary.h"
#if defined(_WIN32)
#include <Windows.h>
#endif
#pragma comment(lib, "enkiTS.lib")

namespace
{
    thread_local bool executing_pool_work = false;

    // 워커 수명 훅 저장소. 이 층은 관측 도구를 모른다 — 받아 둔 함수를 부르기만
    // 한다. 정적인 이유는 enkiTS 콜백이 userData 없는 함수 포인터이기 때문이고,
    // 엔진 소유 영속 풀이 하나뿐이라 그것으로 충분하다.
    thread_pool::worker_hooks g_thread_pool_worker_hooks{};

    // 배정 스레드와 합류 지원 슬롯은 enkiTS 외부 번호 1, 2를 사용한다.
    // 워커 훅에는 기존 1..N을 넘겨 프로파일러 레인 신원을 유지한다.
    constexpr uint32_t kDispatcherThreads = 1;
    constexpr uint32_t kExternalThreads = kDispatcherThreads + 1;

    uint32_t thread_pool_os_thread_id() noexcept
    {
#if defined(_WIN32)
        return GetCurrentThreadId();
#else
        return 0;
#endif
    }

    void thread_pool_worker_thread_start(uint32_t threadnum)
    {
#if defined(_WIN32)
        // ETW에서 풀 워커와 렌더 스레드의 Ready 구간을 분리해 식별한다.
        wchar_t name[32];
        std::swprintf(name, std::size(name), L"Creator.JobWorker.%u", threadnum - kExternalThreads);
        SetThreadDescription(GetCurrentThread(), name);
#endif
        if (g_thread_pool_worker_hooks.on_start)
        {
            g_thread_pool_worker_hooks.on_start(threadnum - kExternalThreads);
        }
    }
    void thread_pool_worker_thread_stop(uint32_t threadnum)
    {
        if (g_thread_pool_worker_hooks.on_stop)
        {
            g_thread_pool_worker_hooks.on_stop(threadnum - kExternalThreads);
        }
    }
} // namespace

void thread_pool_worker_idle(uint32_t)
{
    if (g_thread_pool_worker_hooks.on_idle)
    {
        g_thread_pool_worker_hooks.on_idle();
    }
}

struct thread_pool::state
{
    struct work : enki::ITaskSet
    {
        state& owner_;
        std::function<void(std::size_t)> execute_;
        std::function<void(std::exception_ptr)> complete_;
        std::mutex mutex_;
        std::exception_ptr failure_;
        struct completion : enki::ICompletable
        {
            enki::Dependency dependency_;
            void OnDependenciesComplete(enki::TaskScheduler* scheduler, uint32_t thread) override
            {
                const bool previous = std::exchange(executing_pool_work, true);
                enki::ICompletable::OnDependenciesComplete(scheduler, thread);
                auto* task = const_cast<work*>(static_cast<const work*>(dependency_.GetDependencyTask()));
                auto& owner = task->owner_;
                auto complete = std::move(task->complete_);
                const auto error = task->failure_;
                delete task; // Release user captures before publishing completion.
                complete(error);
                owner.finish();
                executing_pool_work = previous;
            }
        } completion_;

        work(state& owner, std::size_t count, std::function<void(std::size_t)> execute,
             std::function<void(std::exception_ptr)> complete)
            : enki::ITaskSet(static_cast<uint32_t>(count)), owner_(owner), execute_(std::move(execute)),
              complete_(std::move(complete))
        {
            completion_.SetDependency(completion_.dependency_, this);
        }
        void ExecuteRange(enki::TaskSetPartition range, uint32_t) override
        {
            const bool previous = std::exchange(executing_pool_work, true);
            for (auto i = range.start; i < range.end; ++i)
            {
                try
                {
                    execute_(i);
                }
                catch (...)
                {
                    std::lock_guard lock(mutex_);
                    if (!failure_)
                    {
                        failure_ = std::current_exception();
                    }
                }
            }
            executing_pool_work = previous;
        }
    };

    // One handoff per batch, not per item. Publishing from any host thread never
    // executes the user's callback there, even when an enkiTS pipe is full.
    // 전달은 dispatcher에서 처리한다. enkiTS pipe가 가득 차면 그 dispatcher가
    // 본문을 실행할 수 있지만 일반 제출 스레드는 본문을 실행하지 않는다.
    // It was pinned to the next worker in turn; a worker inside a long task
    // cannot run pinned tasks, so the batch waited for that task to finish
    // while the other workers sat idle (animation joins waited up to 33 ms).
    struct handoff : enki::IPinnedTask
    {
        state& owner_;
        work* work_;
        std::atomic<unsigned> references_{2};
        struct completion : enki::ICompletable
        {
            enki::Dependency dependency_;
            std::atomic<unsigned>& references_;
            explicit completion(std::atomic<unsigned>& references) : references_(references) {}
            void OnDependenciesComplete(enki::TaskScheduler* scheduler, uint32_t thread) override
            {
                enki::ICompletable::OnDependenciesComplete(scheduler, thread);
                auto* task = dependency_.GetDependencyTask();
                if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1)
                {
                    delete task;
                }
            }
        } completion_{references_};
        handoff(state& owner, work* task, uint32_t thread) : enki::IPinnedTask(thread), owner_(owner), work_(task)
        {
            completion_.SetDependency(completion_.dependency_, this);
        }
        void Execute() override { owner_.scheduler_.AddTaskSetToPipe(work_); }
        void release_submission()
        {
            if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                delete this;
            }
        }
    };

    // 모든 native 의존성을 한 번에 고정한 뒤 하나의 pinned root만 게시한다.
    // 후속 노드는 선행 완료 스레드의 enki pipe로 바로 전달하므로 dispatcher 왕복이 없다.
    struct graph_work
    {
        enum class gate_kind
        {
            admission,
            ready,
            complete,
            terminal
        };

        struct gate : enki::ICompletable
        {
            graph_work& graph_;
            gate_kind kind_;
            std::size_t index_{};

            gate(graph_work& graph, gate_kind kind, std::size_t index = 0) : graph_(graph), kind_(kind), index_(index)
            {
            }

            void OnDependenciesComplete(enki::TaskScheduler* scheduler, uint32_t thread) noexcept override
            {
                // enki의 마지막 dependency counter RMW가 읽은 release sequence를 획득한다.
                std::atomic_thread_fence(std::memory_order_acquire);
                const bool previous = std::exchange(executing_pool_work, true);
                // enki는 완료 알림 중 다음 간선 포인터를 보관한다. 마지막 본문이 끝났어도
                // 이 순회가 반환할 때까지 전체 간선과 노드의 수명을 유지해야 한다.
                auto* lifetime = &graph_;
                lifetime->retain();
                auto* diagnostics = graph_.diagnostics_.get();
                if (diagnostics)
                {
                    if (kind_ == gate_kind::ready)
                    {
                        auto& timing = diagnostics->nodes[index_];
                        timing.ready_ns = job_timestamp_ns();
                        timing.publish_thread = thread;
                        timing.publish_os_thread = thread_pool_os_thread_id();
                        timing.dispatcher_route = thread == dispatcher_thread_;
                        timing.publish_begin_ns = job_timestamp_ns();
                    }
                    else if (kind_ == gate_kind::complete)
                    {
                        diagnostics->nodes[index_].complete_ns = job_timestamp_ns();
                    }
                }
                enki::ICompletable::OnDependenciesComplete(scheduler, thread);
                if (diagnostics && kind_ == gate_kind::ready)
                {
                    diagnostics->nodes[index_].publish_end_ns = job_timestamp_ns();
                }
                if (kind_ == gate_kind::terminal)
                {
                    // 다른 완료 콜백과 제출 스레드의 참조까지 빠진 뒤 소멸자가 최종 완료를 게시한다.
                    graph_.release();
                }
                lifetime->release();
                executing_pool_work = previous;
            }
        };

        struct node : enki::ITaskSet
        {
            graph_work& graph_;
            std::size_t index_;
            std::vector<uint32_t> prerequisites_;
            std::atomic<bool> failed_{false};
            gate ready_;
            gate complete_;

            node(graph_work& graph, std::size_t index, const job_graph_node& description)
                : enki::ITaskSet(1), graph_(graph), index_(index), prerequisites_(description.dependencies),
                  ready_(graph, gate_kind::ready, index), complete_(graph, gate_kind::complete, index)
            {
            }

            void ExecuteRange(enki::TaskSetPartition, uint32_t thread) override
            {
                const bool previous = std::exchange(executing_pool_work, true);
                auto* timing = graph_.diagnostics_ ? &graph_.diagnostics_->nodes[index_] : nullptr;
                if (timing)
                {
                    timing->start_ns = job_timestamp_ns();
                    timing->execute_thread = thread;
                    timing->execute_os_thread = thread_pool_os_thread_id();
                }
                bool skipped = false;
                for (const auto prerequisite : prerequisites_)
                {
                    if (graph_.nodes_[prerequisite]->failed_.load(std::memory_order_acquire))
                    {
                        skipped = true;
                        break;
                    }
                }
                if (skipped)
                {
                    failed_.store(true, std::memory_order_release);
                }
                else
                {
                    try
                    {
                        graph_.execute_(index_);
                    }
                    catch (...)
                    {
                        graph_.remember_failure(std::current_exception());
                        failed_.store(true, std::memory_order_release);
                    }
                }
                if (timing)
                {
                    timing->skipped = skipped;
                    timing->body_end_ns = job_timestamp_ns();
                }
                executing_pool_work = previous;
            }
        };

        struct delivery : enki::IPinnedTask
        {
            graph_work& graph_;
            explicit delivery(graph_work& graph) : enki::IPinnedTask(dispatcher_thread_), graph_(graph) {}
            void Execute() override
            {
                if (graph_.diagnostics_)
                {
                    graph_.diagnostics_->dispatcher_begin_ns = job_timestamp_ns();
                }
            }
        };

        state& owner_;
        std::function<void(std::size_t)> execute_;
        std::function<void(std::exception_ptr)> complete_;
        std::shared_ptr<job_graph_diagnostics> diagnostics_;
        // 수락된 그래프와 제출 호출이 각각 하나씩 보관한다. 완료 순회는 진입/반환 때 증감한다.
        std::atomic<uint64_t> references_{2};
        std::mutex failure_mutex_;
        std::exception_ptr failure_;
        bool admitted_{};
        delivery delivery_{*this};
        gate admission_{*this, gate_kind::admission};
        gate terminal_{*this, gate_kind::terminal};
        std::vector<std::unique_ptr<node>> nodes_;
        // 마지막 멤버로 두어 모든 노드를 파괴하기 전에 간선을 먼저 해제한다.
        std::vector<enki::Dependency> dependencies_;

        graph_work(state& owner, std::span<const job_graph_node> nodes, std::function<void(std::size_t)> execute,
                   std::function<void(std::exception_ptr)> complete, std::shared_ptr<job_graph_diagnostics> diagnostics)
            : owner_(owner), execute_(std::move(execute)), complete_(std::move(complete)),
              diagnostics_(std::move(diagnostics))
        {
            nodes_.reserve(nodes.size());
            std::size_t edge_count = 2;
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const auto prerequisite_count = (std::max)(std::size_t(1), nodes[index].dependencies.size());
                const auto maximum_edges = (std::numeric_limits<std::size_t>::max)();
                if (prerequisite_count > maximum_edges - 3 || edge_count > maximum_edges - (prerequisite_count + 3))
                {
                    throw std::invalid_argument("job graph edge count overflow");
                }
                edge_count += prerequisite_count + 3;
                nodes_.push_back(std::make_unique<node>(*this, index, nodes[index]));
                if (diagnostics_)
                {
                    diagnostics_->nodes[index].registered_ns = job_timestamp_ns();
                }
            }
            dependencies_.resize(edge_count);
            std::size_t edge = 0;
            const auto connect = [&](const enki::ICompletable* from, enki::ICompletable* to)
            { dependencies_[edge++].SetDependency(from, to); };
            connect(&delivery_, &admission_);
            connect(&admission_, &terminal_);
            for (const auto& task : nodes_)
            {
                // native task마다 완료 gate 하나만 둬 내부 순회에서 수명을 보호한다.
                connect(task.get(), &task->complete_);
                connect(&task->complete_, &terminal_);
                connect(&task->ready_, task.get());
                if (task->prerequisites_.empty())
                {
                    connect(&admission_, &task->ready_);
                }
                else
                {
                    for (const auto prerequisite : task->prerequisites_)
                    {
                        connect(&nodes_[prerequisite]->complete_, &task->ready_);
                    }
                }
            }
        }

        bool matches(std::span<const job_graph_node> descriptions) const
        {
            if (nodes_.size() != descriptions.size())
            {
                return false;
            }
            for (size_t index = 0; index < descriptions.size(); ++index)
            {
                if (nodes_[index]->prerequisites_ != descriptions[index].dependencies)
                {
                    return false;
                }
            }
            return true;
        }

        void prepare(std::function<void(std::size_t)> execute, std::function<void(std::exception_ptr)> complete,
                     std::shared_ptr<job_graph_diagnostics> diagnostics)
        {
            execute_ = std::move(execute);
            complete_ = std::move(complete);
            diagnostics_ = std::move(diagnostics);
            if (diagnostics_)
            {
                diagnostics_->native_storage_reused = true;
            }
            references_.store(2, std::memory_order_relaxed);
            for (auto& task : std::span(nodes_.data(), nodes_.size()))
            {
                task->failed_.store(false, std::memory_order_relaxed);
                if (diagnostics_)
                {
                    diagnostics_->nodes[task->index_].registered_ns = job_timestamp_ns();
                }
            }
        }

        // Called only after the last enki callback traversal has returned.
        // No member may be accessed after recycling publishes this object.
        void retire() noexcept
        {
            auto& owner = owner_;
            execute_ = {};
            if (diagnostics_)
            {
                diagnostics_->completed_ns = job_timestamp_ns();
            }
            diagnostics_.reset();
            auto complete = std::move(complete_);
            auto failure = std::exchange(failure_, {});
            admitted_ = false;
            owner.recycle_graph(this);
            complete(failure);
            complete = {};
            owner.finish();
        }

        void remember_failure(std::exception_ptr error)
        {
            std::lock_guard lock(failure_mutex_);
            if (!failure_)
            {
                failure_ = error;
            }
        }

        void retain() noexcept { references_.fetch_add(1, std::memory_order_relaxed); }

        void release() noexcept
        {
            // 마지막 감소는 이전 모든 콜백의 쓰기를 획득한다. publish_end도 최종 handle보다 앞선다.
            if (references_.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                retire();
            }
        }
    };

    // Wakes the dispatcher for shutdown; it has nothing to publish.
    struct wake : enki::IPinnedTask
    {
        using enki::IPinnedTask::IPinnedTask;
        void Execute() override {}
    };

    static_assert(std::atomic<graph_work*>::is_always_lock_free);
    std::array<std::atomic<graph_work*>, 8> completed_graphs_{};

    void recycle_graph(graph_work* graph) noexcept
    {
        if (graph->nodes_.size() <= 4096 && graph->dependencies_.size() <= 16384)
        {
            for (auto& slot : completed_graphs_)
            {
                graph_work* empty{};
                if (slot.compare_exchange_strong(empty, graph, std::memory_order_release, std::memory_order_relaxed))
                {
                    return;
                }
            }
        }
        delete graph;
    }

    std::unique_ptr<graph_work> acquire_graph(std::span<const job_graph_node> nodes)
    {
        for (auto& slot : completed_graphs_)
        {
            if (!slot.load(std::memory_order_relaxed))
            {
                continue;
            }
            auto* graph = slot.exchange(nullptr, std::memory_order_acquire);
            if (!graph)
            {
                continue;
            }
            if (graph->matches(nodes))
            {
                return std::unique_ptr<graph_work>(graph);
            }
            graph_work* empty{};
            if (!slot.compare_exchange_strong(empty, graph, std::memory_order_release, std::memory_order_relaxed))
            {
                delete graph;
            }
        }
        return {};
    }

    enki::TaskScheduler scheduler_;
    std::mutex submit_mutex_, done_mutex_, assist_mutex_;
    std::condition_variable done_;
    std::size_t pending_{};
    uint32_t workers_{};
    bool accepting_{true};
    // pinned 전달을 위한 enkiTS 외부 스레드다. 평소 루프는 배정만 하지만
    // pipe가 가득 차면 enkiTS가 이 스레드에서 본문을 실행할 수 있다.
    static constexpr uint32_t dispatcher_thread_ = enki::TaskScheduler::GetNumFirstExternalTaskThread();
    std::thread dispatcher_;
    std::atomic<bool> dispatcher_stop_{false};
    wake dispatcher_wake_{dispatcher_thread_};

    explicit state(std::size_t workers)
    {
        if (workers > (std::numeric_limits<uint32_t>::max)() - kExternalThreads - 1)
        {
            throw std::invalid_argument("thread_pool worker count overflow");
        }
        workers_ = workers ? static_cast<uint32_t>(workers) : (std::max)(1u, enki::GetNumHardwareThreads());
        if (workers_ > (std::numeric_limits<uint32_t>::max)() - kExternalThreads - 1)
        {
            throw std::invalid_argument("thread_pool worker count overflow");
        }
        enki::TaskSchedulerConfig config;
        config.numTaskThreadsToCreate = workers_;
        config.numExternalTaskThreads = kExternalThreads;
        // 워커 수명을 밖으로 알린다. enkiTS 콜백은 함수 포인터라 userData 가
        // 없으므로 트램펄린이 정적 훅을 읽는다.
        config.profilerCallbacks.threadStart = &thread_pool_worker_thread_start;
        config.profilerCallbacks.threadStop = &thread_pool_worker_thread_stop;
        config.profilerCallbacks.waitForNewTaskSuspendStart = &thread_pool_worker_idle;
        config.profilerCallbacks.waitForTaskCompleteSuspendStart = &thread_pool_worker_idle;
        scheduler_.Initialize(config);
        std::promise<bool> registered;
        auto ready = registered.get_future();
        dispatcher_ = std::thread(
            [this, &registered]
            {
#if defined(_WIN32)
                SetThreadDescription(GetCurrentThread(), L"Creator.JobDispatcher");
#endif
                const bool ok = scheduler_.RegisterExternalTaskThread(dispatcher_thread_);
                registered.set_value(ok);
                if (!ok)
                {
                    return;
                }
                for (;;)
                {
                    scheduler_.WaitForNewPinnedTasks();
                    scheduler_.RunPinnedTasks();
                    if (dispatcher_stop_.load(std::memory_order_acquire))
                    {
                        break;
                    }
                }
                scheduler_.DeRegisterExternalTaskThread();
            });
        if (!ready.get())
        {
            dispatcher_.join();
            scheduler_.WaitforAllAndShutdown();
            throw std::runtime_error("thread_pool dispatcher registration failed");
        }
    }
    ~state()
    {
        stop_dispatcher();
        for (auto& slot : completed_graphs_)
        {
            delete slot.load(std::memory_order_relaxed);
        }
    }
    // Called after the last handoff has drained; the wake task is the final
    // pinned task the dispatcher runs before it deregisters.
    void stop_dispatcher() noexcept
    {
        if (!dispatcher_.joinable())
        {
            return;
        }
        dispatcher_stop_.store(true, std::memory_order_release);
        scheduler_.AddPinnedTask(&dispatcher_wake_);
        dispatcher_.join();
    }
    void dispatch(std::size_t count, std::function<void(std::size_t)> execute,
                  std::function<void(std::exception_ptr)> complete)
    {
        if (!count || count > (std::numeric_limits<uint32_t>::max)())
        {
            throw std::invalid_argument("thread_pool invalid batch size");
        }
        std::lock_guard lock(submit_mutex_);
        if (!accepting_)
        {
            throw std::runtime_error("thread_pool is stopped");
        }
        auto task = std::make_unique<work>(*this, count, std::move(execute), std::move(complete));
        auto delivery = std::make_unique<handoff>(*this, task.get(), dispatcher_thread_);
        {
            std::lock_guard count_lock(done_mutex_);
            ++pending_;
        }
        task.release();
        auto* submitted = delivery.release();
        scheduler_.AddPinnedTask(submitted);
        submitted->release_submission();
    }
    void dispatch_graph(std::span<const job_graph_node> nodes, std::function<void(std::size_t)> execute,
                        std::function<void(std::exception_ptr)> complete,
                        std::shared_ptr<job_graph_diagnostics> diagnostics)
    {
        auto graph = acquire_graph(nodes);
        if (graph)
        {
            graph->prepare(std::move(execute), std::move(complete), std::move(diagnostics));
        }
        else
        {
            graph = std::make_unique<graph_work>(*this, nodes, std::move(execute), std::move(complete),
                                                 std::move(diagnostics));
        }
        graph_work* submitted = nullptr;
        {
            std::lock_guard lock(submit_mutex_);
            if (!accepting_)
            {
                throw std::runtime_error("thread_pool is stopped");
            }
            {
                std::lock_guard count_lock(done_mutex_);
                ++pending_;
            }
            graph->admitted_ = true;
            if (graph->diagnostics_)
            {
                graph->diagnostics_->dispatcher_handoffs = 1;
                graph->diagnostics_->native_task_count = static_cast<uint32_t>(nodes.size());
            }
            submitted = graph.release();
            scheduler_.AddPinnedTask(&submitted->delivery_);
        }
        // 완료가 제출보다 빨랐어도 graph 소멸자의 후속 제출이 submit_mutex_와 교착하지 않게 한다.
        submitted->release();
    }
    void assist_until(const std::function<bool()>& ready)
    {
        // One extra external slot, no additional OS worker. Never take scheduler
        // or job completion locks while executing a callback on the joining owner.
        std::unique_lock assist(assist_mutex_, std::try_to_lock);
        if (!assist || ready())
        {
            return;
        }
        const bool external = scheduler_.GetThreadNum() == enki::NO_THREAD_NUM;
        if (external && !scheduler_.RegisterExternalTaskThread(dispatcher_thread_ + 1))
        {
            return;
        }
        try
        {
            while (!ready())
            {
                scheduler_.WaitforTask(nullptr);
                std::unique_lock lock(done_mutex_);
                done_.wait_for(lock, std::chrono::microseconds(100), ready);
            }
        }
        catch (...)
        {
            if (external)
            {
                scheduler_.DeRegisterExternalTaskThread();
            }
            throw;
        }
        if (external)
        {
            scheduler_.DeRegisterExternalTaskThread();
        }
    }
    void finish()
    {
        std::lock_guard lock(done_mutex_);
        --pending_;
        done_.notify_all();
    }
    void close() noexcept
    {
        {
            std::lock_guard lock(submit_mutex_);
            accepting_ = false;
        }
        {
            std::unique_lock lock(done_mutex_);
            done_.wait(lock, [this] { return pending_ == 0; });
        }
        std::lock_guard assist(assist_mutex_);
        stop_dispatcher();
        scheduler_.WaitforAllAndShutdown();
    }
};

thread_pool::thread_pool() = default;
thread_pool::~thread_pool()
{
    shutdown();
}
void thread_pool::set_worker_hooks(const worker_hooks& hooks)
{
    g_thread_pool_worker_hooks = hooks;
}
void thread_pool::start(std::size_t worker_count)
{
    std::lock_guard lock(mutex_);
    if (!state_)
    {
        state_ = std::make_shared<state>(worker_count);
    }
}
std::shared_ptr<thread_pool::state> thread_pool::snapshot() const
{
    std::lock_guard lock(mutex_);
    return state_;
}
bool thread_pool::is_running() const
{
    return bool(snapshot());
}
std::size_t thread_pool::size() const
{
    auto state = snapshot();
    return state ? state->workers_ : 0;
}
bool thread_pool::is_worker_thread() noexcept
{
    return executing_pool_work;
}
void thread_pool::shutdown() noexcept
{
    if (is_worker_thread())
    {
        std::terminate();
    }
    std::shared_ptr<state> state;
    {
        std::lock_guard lock(mutex_);
        state = std::exchange(state_, {});
    }
    if (state)
    {
        state->close();
    }
}
void thread_pool::dispatch(std::size_t count, std::function<void(std::size_t)> execute,
                           std::function<void(std::exception_ptr)> complete)
{
    auto state = snapshot();
    if (!state)
    {
        throw std::runtime_error("thread_pool is stopped");
    }
    state->dispatch(count, std::move(execute), std::move(complete));
}
void thread_pool::dispatch_graph(std::span<const job_graph_node> nodes, std::function<void(std::size_t)> execute,
                                 std::function<void(std::exception_ptr)> complete,
                                 std::shared_ptr<job_graph_diagnostics> diagnostics)
{
    auto state = snapshot();
    if (!state)
    {
        throw std::runtime_error("thread_pool is stopped");
    }
    state->dispatch_graph(nodes, std::move(execute), std::move(complete), std::move(diagnostics));
}

void thread_pool::assist_until(const std::function<bool()>& ready)
{
    if (const auto state = snapshot())
    {
        state->assist_until(ready);
    }
}
