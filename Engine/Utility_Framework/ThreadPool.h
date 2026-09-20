#pragma once
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>

class job_scheduler;

// Engine-owned, persistent enkiTS workers. Consumers submit through job_scheduler.
class thread_pool
{
  public:
    thread_pool();
    ~thread_pool();
    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    // Lifecycle calls are serialized by the engine owner, never by a worker.
    void start(std::size_t worker_count = 0);
    void shutdown() noexcept;
    bool is_running() const;
    std::size_t size() const;
    static bool is_worker_thread() noexcept;

  private:
    friend class job_scheduler;
    struct state;
    void dispatch(std::size_t count, std::function<void(std::size_t)> execute,
                  std::function<void(std::exception_ptr)> complete);
    std::shared_ptr<state> snapshot() const;
    mutable std::mutex mutex_;
    std::shared_ptr<state> state_;
};

namespace ce
{
thread_pool& get_thread_pool();
}
