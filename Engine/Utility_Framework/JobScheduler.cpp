#include "JobScheduler.h"
#include "StandardLibrary.h"

namespace
{
// Completion propagation is iterative: long empty/failed dependency chains must
// not recurse through thousands of finish() calls on a worker's stack.
void notify_dependents(std::vector<std::function<void(std::exception_ptr)>> ready, std::exception_ptr error)
{
    struct notification
    {
        std::function<void(std::exception_ptr)> callback;
        std::exception_ptr error;
    };
    thread_local std::vector<notification> notifications;
    thread_local bool pumping = false;
    for (auto& callback : ready)
        notifications.push_back({std::move(callback), error});
    if (pumping)
        return;
    pumping = true;
    while (!notifications.empty())
    {
        auto next = std::move(notifications.back());
        notifications.pop_back();
        next.callback(next.error);
    }
    pumping = false;
}
} // namespace

uint64_t job_timestamp_ns() noexcept
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

namespace job_detail
{
struct scheduler_state
{
    thread_pool& pool_;
    // Dispatch is installed by job_scheduler, the only thread_pool submission owner.
    std::function<void(std::vector<std::function<void()>>, std::function<void(std::exception_ptr)>)> dispatch_;
    std::function<void(std::size_t, std::function<void(std::size_t)>,
                       std::function<void(std::exception_ptr)>)> dispatch_indexed_;
    std::function<void(std::span<const job_graph_node>, std::function<void(std::size_t)>,
                       std::function<void(std::exception_ptr)>,
                       std::shared_ptr<job_graph_diagnostics>)> dispatch_graph_;
    mutable std::mutex mutex_;
    std::condition_variable done_;
    std::size_t pending_{};
    bool accepting_{};
    explicit scheduler_state(thread_pool& pool) : pool_(pool) {}
};

struct completion_state : std::enable_shared_from_this<completion_state>
{
    std::shared_ptr<scheduler_state> owner_;
    mutable std::mutex mutex_;
    std::condition_variable done_;
    bool complete_{}, completing_{}, dispatched_{};
    std::size_t remaining_{};
    std::exception_ptr failure_;
    std::vector<std::function<void()>> tasks_;
    std::function<void(std::size_t)> indexed_task_;
    std::size_t indexed_count_{};
    std::vector<std::function<void(std::exception_ptr)>> continuations_;
    std::function<void(std::exception_ptr)> terminal_observer_;

    void finish(std::exception_ptr error) noexcept
    {
        std::vector<std::function<void()>> discarded;
        std::function<void(std::size_t)> discarded_indexed;
        std::function<void(std::exception_ptr)> terminal_observer;
        {
            std::lock_guard lock(mutex_);
            if (complete_ || completing_)
                return;
            completing_ = true;
            discarded = std::move(tasks_);
            discarded_indexed = std::move(indexed_task_);
            terminal_observer = std::move(terminal_observer_);
        }
        discarded.clear();
        discarded_indexed = {};
        if (terminal_observer)
        {
            try
            {
                terminal_observer(error);
            }
            catch (...)
            {
                if (!error)
                {
                    error = std::current_exception();
                }
            }
            terminal_observer = {};
        }
        std::vector<std::function<void(std::exception_ptr)>> ready;
        {
            std::lock_guard lock(mutex_);
            failure_ = error;
            // callback/observer 캡처 해제와 최종 오류 저장보다 앞서 완료를 보이면 안 된다.
            complete_ = true;
            ready = std::move(continuations_);
        }
        done_.notify_all();
        notify_dependents(std::move(ready), error);
        std::lock_guard lock(owner_->mutex_);
        if (--owner_->pending_ == 0)
            owner_->done_.notify_all();
    }
    void prerequisite_complete(std::exception_ptr error) noexcept
    {
        std::vector<std::function<void()>> work;
        std::function<void(std::size_t)> indexed_task;
        std::size_t indexed_count{};
        {
            std::lock_guard lock(mutex_);
            if (complete_ || completing_ || dispatched_)
                return;
            if (error && !failure_)
                failure_ = error;
            if (--remaining_ != 0)
                return;
            dispatched_ = true;
            error = failure_;
            work = std::move(tasks_);
            indexed_task = std::move(indexed_task_);
            indexed_count = indexed_count_;
        }
        if (error || (work.empty() && !indexed_count))
        {
            work.clear();
            indexed_task = {};
            finish(error);
            return;
        }
        try
        {
            auto complete = [self = shared_from_this()](std::exception_ptr result) { self->finish(result); };
            if (indexed_count)
                owner_->dispatch_indexed_(indexed_count, std::move(indexed_task), std::move(complete));
            else
                owner_->dispatch_(std::move(work), std::move(complete));
        }
        catch (...)
        {
            work.clear();
            indexed_task = {};
            finish(std::current_exception());
        }
    }
};
} // namespace job_detail

bool job_handle::is_complete() const
{
    if (!state_)
        return true;
    std::lock_guard lock(state_->mutex_);
    return state_->complete_;
}
void job_handle::wait() const
{
    if (!state_)
    {
        return;
    }
    std::unique_lock lock(state_->mutex_);
    // 완료 게시와 같은 mutex로 판단해 미완료 worker 합류를 거절하고 캡처 수명을 보존한다.
    if (!state_->complete_ && thread_pool::is_worker_thread())
    {
        throw std::logic_error("pool workers must express dependencies, not block on job_handle::wait");
    }
    state_->done_.wait(lock, [this] { return state_->complete_; });
    if (state_->failure_)
    {
        std::rethrow_exception(state_->failure_);
    }
}

job_scheduler::job_scheduler(thread_pool& pool) : state_(std::make_shared<job_detail::scheduler_state>(pool))
{
    state_->dispatch_ = [&pool](std::vector<std::function<void()>> tasks,
                                std::function<void(std::exception_ptr)> complete) {
        const auto count = tasks.size();
        pool.dispatch(count, [tasks = std::move(tasks)](std::size_t index) { tasks[index](); }, std::move(complete));
    };
    state_->dispatch_indexed_ = [&pool](std::size_t count,
                                       std::function<void(std::size_t)> task,
                                       std::function<void(std::exception_ptr)> complete) {
        pool.dispatch(count, std::move(task), std::move(complete));
    };
    state_->dispatch_graph_ = [&pool](std::span<const job_graph_node> nodes,
                                      std::function<void(std::size_t)> task,
                                      std::function<void(std::exception_ptr)> complete,
                                      std::shared_ptr<job_graph_diagnostics> diagnostics)
    {
        pool.dispatch_graph(nodes, std::move(task), std::move(complete), std::move(diagnostics));
    };
}
job_scheduler::~job_scheduler()
{
    shutdown();
}
void job_scheduler::start(std::size_t worker_count)
{
    state_->pool_.start(worker_count);
    std::lock_guard lock(state_->mutex_);
    state_->accepting_ = true;
}
void job_scheduler::shutdown() noexcept
{
    if (thread_pool::is_worker_thread())
    {
        std::terminate();
    }
    {
        std::unique_lock lock(state_->mutex_);
        state_->accepting_ = false;
        state_->done_.wait(lock, [this] { return state_->pending_ == 0; });
    }
    state_->pool_.shutdown();
}
bool job_scheduler::is_running() const
{
    std::lock_guard lock(state_->mutex_);
    return state_->accepting_;
}
job_handle job_scheduler::submit(job_group group)
{
    return submit_after({}, std::move(group));
}
job_handle job_scheduler::submit_after(std::span<const job_handle> dependencies, job_group group)
{
    for (const auto& dependency : dependencies)
        if (!dependency.state_ || dependency.state_->owner_ != state_)
            throw std::invalid_argument("job dependency must belong to this scheduler");
    auto job = std::make_shared<job_detail::completion_state>();
    job->owner_ = state_;
    job->tasks_ = std::move(group.tasks_);
    job->terminal_observer_ = std::move(group.terminal_observer_);
    job->remaining_ = dependencies.size() + 1; // Registration barrier.
    {
        std::lock_guard lock(state_->mutex_);
        if (!state_->accepting_)
            throw std::runtime_error("job_scheduler is stopped");
        ++state_->pending_;
    }
    try
    {
        for (const auto& dependency : dependencies)
        {
            std::exception_ptr error;
            {
                std::lock_guard lock(dependency.state_->mutex_);
                if (!dependency.state_->complete_)
                {
                    dependency.state_->continuations_.emplace_back(
                        [job](std::exception_ptr result) { job->prerequisite_complete(result); });
                    continue;
                }
                error = dependency.state_->failure_;
            }
            job->prerequisite_complete(error);
        }
        job->prerequisite_complete({});
    }
    catch (...)
    {
        job->finish(std::current_exception());
        throw;
    }
    return job_handle(std::move(job));
}
job_handle job_scheduler::submit_indexed(std::size_t count, std::function<void(std::size_t)> task)
{
    if (!count)
        return submit(job_group{});
    if (!task)
        throw std::invalid_argument("indexed job requires a task");
    auto job = std::make_shared<job_detail::completion_state>();
    job->owner_ = state_;
    job->indexed_task_ = std::move(task);
    job->indexed_count_ = count;
    job->remaining_ = 1;
    {
        std::lock_guard lock(state_->mutex_);
        if (!state_->accepting_)
            throw std::runtime_error("job_scheduler is stopped");
        ++state_->pending_;
    }
    job->prerequisite_complete({});
    return job_handle(std::move(job));
}
job_handle job_scheduler::submit_graph(std::span<const job_graph_node> nodes,
                                      std::function<void(std::size_t)> task,
                                      std::shared_ptr<job_graph_diagnostics> diagnostics)
{
    if (nodes.size() >= static_cast<std::size_t>((std::numeric_limits<int32_t>::max)()))
    {
        throw std::invalid_argument("job graph node count overflow");
    }
    if (!nodes.empty() && !task)
    {
        throw std::invalid_argument("job graph requires a task");
    }
    // 잘못된 간선은 일부 root가 실행되기 전에 전체를 거절한다.
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        const auto& dependencies = nodes[index].dependencies;
        if (dependencies.size() > static_cast<std::size_t>((std::numeric_limits<int32_t>::max)()))
        {
            throw std::invalid_argument("job graph dependency count overflow");
        }
        for (std::size_t dependency_index = 0; dependency_index < dependencies.size(); ++dependency_index)
        {
            const auto dependency = dependencies[dependency_index];
            if (dependency >= index)
            {
                throw std::invalid_argument("job graph dependencies must be unique earlier nodes");
            }
            // 짧은 선행 목록을 확인하려고 매 프레임 복사/정렬용 배열을 만들지 않는다.
            for (std::size_t earlier = 0; earlier < dependency_index; ++earlier)
            {
                if (dependencies[earlier] == dependency)
                {
                    throw std::invalid_argument("job graph dependencies must be unique earlier nodes");
                }
            }
        }
    }
    if (diagnostics)
    {
        *diagnostics = {};
        diagnostics->nodes.resize(nodes.size());
        diagnostics->submitted_ns = job_timestamp_ns();
    }
    auto job = std::make_shared<job_detail::completion_state>();
    job->owner_ = state_;
    {
        std::lock_guard lock(state_->mutex_);
        if (!state_->accepting_)
        {
            throw std::runtime_error("job_scheduler is stopped");
        }
        ++state_->pending_;
    }
    if (nodes.empty())
    {
        task = {};
        if (diagnostics)
        {
            diagnostics->completed_ns = job_timestamp_ns();
        }
        job->finish({});
        return job_handle(std::move(job));
    }
    try
    {
        state_->dispatch_graph_(nodes, std::move(task),
            [job](std::exception_ptr error) { job->finish(error); }, std::move(diagnostics));
    }
    catch (...)
    {
        task = {};
        job->finish(std::current_exception());
        throw;
    }
    return job_handle(std::move(job));
}
job_handle job_scheduler::parallel_for(std::size_t count, std::size_t grain,
                                       std::function<void(std::size_t, std::size_t)> task)
{
    if (!grain)
        throw std::invalid_argument("parallel_for grain must be positive");
    job_group group;
    for (std::size_t begin = 0; begin < count;)
    {
        const auto end = begin + (std::min)(grain, count - begin);
        group.add([task, begin, end] { task(begin, end); });
        begin = end;
    }
    return submit(std::move(group));
}

namespace
{
struct engine_jobs
{
    thread_pool pool_;
    job_scheduler scheduler_{pool_};
};
engine_jobs& jobs()
{
    static engine_jobs instance;
    return instance;
}
} // namespace
thread_pool& ce::get_thread_pool()
{
    return jobs().pool_;
}
job_scheduler& ce::get_job_scheduler()
{
    return jobs().scheduler_;
}
