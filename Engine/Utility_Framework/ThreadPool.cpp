#include "ThreadPool.h"
#include <enkiTS/TaskScheduler.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <stdexcept>
#include <utility>
#pragma comment(lib, "enkiTS.lib")

namespace
{
thread_local bool executing_pool_work = false;
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
                        failure_ = std::current_exception();
                }
            }
            executing_pool_work = previous;
        }
    };

    // One handoff per batch, not per item. Publishing from any host thread never
    // executes the user's callback there, even when an enkiTS pipe is full.
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
                    delete task;
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
                delete this;
        }
    };

    enki::TaskScheduler scheduler_;
    std::mutex submit_mutex_, done_mutex_;
    std::condition_variable done_;
    std::size_t pending_{};
    uint32_t next_worker_{}, workers_{};
    bool accepting_{true};

    explicit state(std::size_t workers)
    {
        if (workers > (std::numeric_limits<uint32_t>::max)() - 1)
            throw std::invalid_argument("thread_pool worker count overflow");
        workers_ = workers ? static_cast<uint32_t>(workers) : (std::max)(1u, enki::GetNumHardwareThreads());
        enki::TaskSchedulerConfig config;
        config.numTaskThreadsToCreate = workers_;
        scheduler_.Initialize(config);
    }
    void dispatch(std::size_t count, std::function<void(std::size_t)> execute,
                  std::function<void(std::exception_ptr)> complete)
    {
        if (!count || count > (std::numeric_limits<uint32_t>::max)())
            throw std::invalid_argument("thread_pool invalid batch size");
        std::lock_guard lock(submit_mutex_);
        if (!accepting_)
            throw std::runtime_error("thread_pool is stopped");
        auto task = std::make_unique<work>(*this, count, std::move(execute), std::move(complete));
        auto delivery = std::make_unique<handoff>(*this, task.get(), 1u + next_worker_++ % workers_);
        {
            std::lock_guard count_lock(done_mutex_);
            ++pending_;
        }
        task.release();
        auto* submitted = delivery.release();
        scheduler_.AddPinnedTask(submitted);
        submitted->release_submission();
    }
    void finish()
    {
        std::lock_guard lock(done_mutex_);
        if (--pending_ == 0)
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
        scheduler_.WaitforAllAndShutdown();
    }
};

thread_pool::thread_pool() = default;
thread_pool::~thread_pool()
{
    shutdown();
}
void thread_pool::start(std::size_t worker_count)
{
    std::lock_guard lock(mutex_);
    if (!state_)
        state_ = std::make_shared<state>(worker_count);
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
        std::terminate();
    std::shared_ptr<state> state;
    {
        std::lock_guard lock(mutex_);
        state = std::exchange(state_, {});
    }
    if (state)
        state->close();
}
void thread_pool::dispatch(std::size_t count, std::function<void(std::size_t)> execute,
                           std::function<void(std::exception_ptr)> complete)
{
    auto state = snapshot();
    if (!state)
        throw std::runtime_error("thread_pool is stopped");
    state->dispatch(count, std::move(execute), std::move(complete));
}
