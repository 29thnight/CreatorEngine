#include "JobScheduler.h"
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <stdexcept>

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

namespace job_detail
{
struct scheduler_state
{
    thread_pool& pool_;
    // Dispatch is installed by job_scheduler, the only thread_pool submission owner.
    std::function<void(std::vector<std::function<void()>>, std::function<void(std::exception_ptr)>)> dispatch_;
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
    std::vector<std::function<void(std::exception_ptr)>> continuations_;

    void finish(std::exception_ptr error) noexcept
    {
        std::vector<std::function<void()>> discarded;
        {
            std::lock_guard lock(mutex_);
            if (complete_ || completing_)
                return;
            completing_ = true;
            discarded = std::move(tasks_);
        }
        discarded.clear();
        std::vector<std::function<void(std::exception_ptr)>> ready;
        {
            std::lock_guard lock(mutex_);
            failure_ = error;
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
        }
        if (error || work.empty())
        {
            work.clear();
            finish(error);
            return;
        }
        try
        {
            owner_->dispatch_(std::move(work),
                              [self = shared_from_this()](std::exception_ptr result) { self->finish(result); });
        }
        catch (...)
        {
            work.clear();
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
        return;
    std::unique_lock lock(state_->mutex_);
    if (!state_->complete_ && thread_pool::is_worker_thread())
        throw std::logic_error("pool workers must express dependencies, not block on job_handle::wait");
    state_->done_.wait(lock, [this] { return state_->complete_; });
    if (state_->failure_)
        std::rethrow_exception(state_->failure_);
}

job_scheduler::job_scheduler(thread_pool& pool) : state_(std::make_shared<job_detail::scheduler_state>(pool))
{
    state_->dispatch_ = [&pool](std::vector<std::function<void()>> tasks,
                                std::function<void(std::exception_ptr)> complete) {
        const auto count = tasks.size();
        pool.dispatch(count, [tasks = std::move(tasks)](std::size_t index) { tasks[index](); }, std::move(complete));
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
        std::terminate();
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
