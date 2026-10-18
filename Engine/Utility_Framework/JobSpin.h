#pragma once
#include "StandardLibrary.h"

struct job_spin_options
{
    std::uint64_t frame_id{};
    std::chrono::microseconds idle_budget{};
    std::chrono::microseconds frame_budget{};
    std::uint32_t max_workers{};
};

struct job_spin_statistics
{
    std::uint64_t frame_id{};
    std::uint64_t spin_attempts{};
    std::uint64_t task_hits{};
    std::uint64_t budget_exhaustions{};
    std::uint64_t scope_exits{};
    std::uint64_t shutdown_exits{};
    // OS 선점 시간을 포함하는 추가 spin 구간의 wall time이며 CPU 작업 시간이 아니다.
    std::uint64_t spin_wall_ns{};
    // 허가 시 미리 차감한 시간. 사용하지 않은 부분도 반환하지 않아 프레임 예산을 보수적으로 지킨다.
    std::uint64_t reserved_budget_ns{};
    std::uint32_t active_spinners{};
    std::uint32_t max_active_spinners{};
};

namespace job_spin_detail
{
    class scope_state
    {
      public:
        virtual ~scope_state() = default;
        virtual void close() noexcept = 0;
        virtual job_spin_statistics statistics() const noexcept = 0;
    };
}

class thread_pool_spin_control;

// 이동 가능한 기록 범위. close는 worker를 기다리지 않고 추가 spin 허가만 닫는다.
// 닫힌 token도 statistics를 보관하며 선점된 worker가 빠져나오면 최종 값이 반영된다.
class job_spin_scope
{
  public:
    job_spin_scope() = default;
    job_spin_scope(const job_spin_scope&) = delete;
    job_spin_scope& operator=(const job_spin_scope&) = delete;

    job_spin_scope(job_spin_scope&& other) noexcept
        : state_(std::move(other.state_)), open_(std::exchange(other.open_, false))
    {
    }

    job_spin_scope& operator=(job_spin_scope&& other) noexcept
    {
        if (this != &other)
        {
            close();
            state_ = std::move(other.state_);
            open_ = std::exchange(other.open_, false);
        }
        return *this;
    }

    ~job_spin_scope()
    {
        close();
    }

    explicit operator bool() const noexcept
    {
        return open_;
    }

    void close() noexcept
    {
        if (std::exchange(open_, false))
        {
            state_->close();
        }
    }

    void reset() noexcept
    {
        close();
    }

    job_spin_statistics statistics() const noexcept
    {
        return state_ ? state_->statistics() : job_spin_statistics{};
    }

  private:
    friend class thread_pool_spin_control;
    explicit job_spin_scope(std::shared_ptr<job_spin_detail::scope_state> state) noexcept
        : state_(std::move(state)), open_(state_ != nullptr)
    {
    }

    std::shared_ptr<job_spin_detail::scope_state> state_;
    bool open_{};
};
