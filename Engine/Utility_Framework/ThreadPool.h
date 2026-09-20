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

    // 워커 수명 훅. 관측 도구(프로파일러)가 워커의 시작·종료를 알아야 하는데,
    // 이 층이 그 도구를 알면 경계가 뒤집힌다 — 그래서 **함수를 받아 두고 부르기만
    // 한다**. enkiTS 의 threadnum_ 은 0..GetNumTaskThreads()-1 로 안정 보장되므로
    // 받는 쪽이 그대로 슬롯 키로 쓸 수 있다.
    //
    // ★ start() **전에** 걸어야 한다. 워커는 start 에서 만들어지고, 그때 이미
    //   걸려 있는 훅만 그 워커의 시작을 본다.
    struct worker_hooks
    {
        void (*on_start)(unsigned int worker_index) = nullptr;
        void (*on_stop)(unsigned int worker_index) = nullptr;
    };
    static void set_worker_hooks(const worker_hooks& hooks);

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
