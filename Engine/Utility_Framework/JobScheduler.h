#pragma once
#include "ThreadPool.h"
#include "StandardLibrary.h"
#include "JobGraph.h"

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
    // Explicit owner participation. No incomplete worker wait and no callback
    // under a completion mutex; ordinary wait() keeps its blocking semantics.
    void wait_assisting() const;

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
    // One terminal observer for accepted work, including dependency/dispatch
    // failure before a task body runs. Invoked after task captures are released,
    // before the completion token becomes ready; observer captures are also
    // released before readiness. May run inline during submit_after.
    void on_complete(std::function<void(std::exception_ptr)> observer) { terminal_observer_ = std::move(observer); }
    std::size_t size() const noexcept { return tasks_.size(); }
    bool empty() const noexcept { return tasks_.empty(); }

  private:
    friend class job_scheduler;
    std::vector<std::function<void()>> tasks_;
    std::function<void(std::exception_ptr)> terminal_observer_;
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
    // One callback is shared by an indexed batch. No per-index task vector is
    // built; callers keep captured storage alive until the handle completes.
    job_handle submit_indexed(std::size_t count, std::function<void(std::size_t)> task);
    // 전체 그래프를 한 번에 수락하고 한 번만 dispatcher로 전달한다.
    // 선행 실패의 후손은 본문을 건너뛰고 독립 가지는 끝까지 실행한다.
    // 최종 handle은 공유 callback 캡처를 해제한 뒤에만 완료된다.
    job_handle submit_graph(std::span<const job_graph_node> nodes, std::function<void(std::size_t)> task,
                            std::shared_ptr<job_graph_diagnostics> diagnostics = {});
    job_handle parallel_for(std::size_t count, std::size_t grain, std::function<void(std::size_t, std::size_t)> task);

  private:
    std::shared_ptr<job_detail::scheduler_state> state_;
};

namespace ce
{
    job_scheduler& get_job_scheduler();
}
