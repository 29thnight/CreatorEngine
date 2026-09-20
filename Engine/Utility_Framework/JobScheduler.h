#pragma once
#include "ThreadPool.h"
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace job_detail
{
struct completion_state;
struct scheduler_state;
} // namespace job_detail

// Copyable completion token. Dropping it never cancels or waits for the work.
class job_handle
{
  public:
    job_handle() = default;
    bool valid() const noexcept { return bool(state_); }
    bool is_complete() const;
    // Reports this job's error, after its callbacks/captures have been released.
    // An incomplete wait from a pool worker is rejected; use submit_after instead.
    void wait() const;

  private:
    friend class job_scheduler;
    explicit job_handle(std::shared_ptr<job_detail::completion_state> state) : state_(std::move(state)) {}
    std::shared_ptr<job_detail::completion_state> state_;
};

// A single-producer batch builder. submit consumes and seals it before execution.
class job_group
{
  public:
    job_group() = default;
    job_group(job_group&&) = default;
    job_group& operator=(job_group&&) = default;
    job_group(const job_group&) = delete;
    job_group& operator=(const job_group&) = delete;
    template<class F>
    void add(F&& task)
    {
        tasks_.emplace_back(std::forward<F>(task));
    }
    std::size_t size() const noexcept { return tasks_.size(); }
    bool empty() const noexcept { return tasks_.empty(); }

  private:
    friend class job_scheduler;
    std::vector<std::function<void()>> tasks_;
};

class job_scheduler
{
  public:
    // Exclusive submission/lifecycle owner for this pool; pool outlives scheduler.
    explicit job_scheduler(thread_pool& pool);
    ~job_scheduler();
    job_scheduler(const job_scheduler&) = delete;
    job_scheduler& operator=(const job_scheduler&) = delete;
    // Engine lifecycle only: stop admission, drain accepted graphs, then join workers.
    void start(std::size_t worker_count = 0);
    void shutdown() noexcept;
    bool is_running() const;
    job_handle submit(job_group group);
    template<class F>
    job_handle submit(F&& task)
    {
        job_group group;
        group.add(std::forward<F>(task));
        return submit(std::move(group));
    }
    // Dependencies must belong to this scheduler. Failed prerequisites skip the
    // dependent body and propagate the error. Waiting graphs occupy no workers.
    job_handle submit_after(std::span<const job_handle> dependencies, job_group group);
    job_handle parallel_for(std::size_t count, std::size_t grain, std::function<void(std::size_t, std::size_t)> task);

  private:
    std::shared_ptr<job_detail::scheduler_state> state_;
};

namespace ce
{
job_scheduler& get_job_scheduler();
}
