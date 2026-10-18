#pragma once
#include "JobSpin.h"
#include <enkiTS/TaskScheduler.h>

#if !defined(ENKITS_CREATOR_IDLE_SPIN_API) || ENKITS_CREATOR_IDLE_SPIN_API != 1
#error CreatorEngine requires the ports/enkits overlay header and matching enkiTS library.
#endif

namespace job_spin_detail
{
    using clock_function = std::uint64_t (*)() noexcept;

    inline std::uint64_t steady_now_ns() noexcept
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    struct frame_state
    {
        explicit frame_state(job_spin_options options)
            : options_(options), remaining_ns_(static_cast<std::uint64_t>(options.frame_budget.count()) * 1000)
        {
        }

        job_spin_statistics statistics() const noexcept
        {
            job_spin_statistics value;
            value.frame_id = options_.frame_id;
            value.spin_attempts = spin_attempts_.load(std::memory_order_relaxed);
            value.task_hits = task_hits_.load(std::memory_order_relaxed);
            value.budget_exhaustions = budget_exhaustions_.load(std::memory_order_relaxed);
            value.scope_exits = scope_exits_.load(std::memory_order_relaxed);
            value.shutdown_exits = shutdown_exits_.load(std::memory_order_relaxed);
            value.spin_wall_ns = spin_wall_ns_.load(std::memory_order_relaxed);
            value.reserved_budget_ns = reserved_budget_ns_.load(std::memory_order_relaxed);
            value.active_spinners = active_spinners_.load(std::memory_order_relaxed);
            value.max_active_spinners = max_active_spinners_.load(std::memory_order_relaxed);
            return value;
        }

        const job_spin_options options_;
        // scope 수는 control_state::mutex_ 아래에서만 갱신한다. worker는 gate만 읽는다.
        std::uint32_t scopes_{};
        std::atomic<std::uint64_t> remaining_ns_{};
        std::atomic<std::uint64_t> spin_attempts_{};
        std::atomic<std::uint64_t> task_hits_{};
        std::atomic<std::uint64_t> budget_exhaustions_{};
        std::atomic<std::uint64_t> scope_exits_{};
        std::atomic<std::uint64_t> shutdown_exits_{};
        std::atomic<std::uint64_t> spin_wall_ns_{};
        std::atomic<std::uint64_t> reserved_budget_ns_{};
        std::atomic<std::uint32_t> active_spinners_{};
        std::atomic<std::uint32_t> max_active_spinners_{};
    };

    class control_state;

    class scope_token final : public scope_state
    {
      public:
        scope_token(std::shared_ptr<control_state> owner, std::shared_ptr<frame_state> frame)
            : owner_(std::move(owner)), frame_(std::move(frame))
        {
        }

        void close() noexcept override;

        job_spin_statistics statistics() const noexcept override
        {
            return frame_->statistics();
        }

      private:
        std::shared_ptr<control_state> owner_;
        std::shared_ptr<frame_state> frame_;
    };

    class control_state final : public std::enable_shared_from_this<control_state>
    {
      public:
        control_state(std::uint32_t total_thread_slots, clock_function clock)
            : slot_count_(total_thread_slots), slots_(std::make_unique<worker_slot[]>(total_thread_slots)),
              clock_(clock ? clock : &steady_now_ns)
        {
        }

        std::shared_ptr<scope_state> begin_scope(job_spin_options options)
        {
            if (options.idle_budget.count() <= 0 || options.frame_budget.count() <= 0 || options.max_workers == 0)
            {
                return {};
            }
            // 환경 변수 외의 호출도 같은 상한을 적용한다. deadline은 반복 횟수로 환산하지 않는다.
            options.idle_budget = (std::min)(options.idle_budget, std::chrono::microseconds{200});
            options.frame_budget = (std::min)(options.frame_budget, std::chrono::microseconds{2000});
            options.max_workers = (std::min)({options.max_workers, slot_count_, std::uint32_t{8}});
            if (options.max_workers == 0)
            {
                return {};
            }

            std::lock_guard lock(mutex_);
            if (stopped_.load(std::memory_order_acquire))
            {
                return {};
            }
            if (current_ && options.frame_id < current_->options_.frame_id)
            {
                return {};
            }
            if (current_ && current_->options_.frame_id == options.frame_id)
            {
                const auto& fixed = current_->options_;
                if (fixed.idle_budget != options.idle_budget || fixed.frame_budget != options.frame_budget
                    || fixed.max_workers != options.max_workers)
                {
                    // 같은 프레임의 다른 뷰가 예산이나 허가 수를 늘리지 못한다.
                    return {};
                }
            }
            else
            {
                const auto gate = gate_.load(std::memory_order_acquire);
                if ((current_ && current_->scopes_ != 0) || (gate & reference_mask) != 0)
                {
                    // 선점된 이전 프레임 worker를 기다리거나 그 worker의 예산을 재충전하지 않는다.
                    return {};
                }
                auto next = std::make_shared<frame_state>(options);
                current_ = std::move(next);
                current_raw_.store(current_.get(), std::memory_order_release);
            }

            // 할당 실패 시 scope 수와 gate를 열어 둔 채 예외가 나가지 않도록 먼저 만든다.
            auto token = std::make_shared<scope_token>(shared_from_this(), current_);
            if (current_->scopes_ == 0 && !open_gate())
            {
                return {};
            }
            ++current_->scopes_;
            return token;
        }

        void close_scope(const std::shared_ptr<frame_state>& frame) noexcept
        {
            std::lock_guard lock(mutex_);
            if (frame != current_ || frame->scopes_ == 0)
            {
                return;
            }
            --frame->scopes_;
            if (frame->scopes_ == 0)
            {
                gate_.fetch_and(~open_bit, std::memory_order_acq_rel);
            }
        }

        void stop() noexcept
        {
            stopped_.store(true, std::memory_order_release);
            gate_.fetch_and(~open_bit, std::memory_order_acq_rel);
        }

        bool begin(std::uint32_t thread_id) noexcept
        {
            if (thread_id >= slot_count_ || stopped_.load(std::memory_order_acquire))
            {
                return false;
            }
            auto& slot = slots_[thread_id];
            if (slot.frame_ != nullptr)
            {
                return false;
            }
            auto gate = gate_.load(std::memory_order_acquire);
            bool admitted = false;
            for (unsigned int attempt = 0; attempt < 8; ++attempt)
            {
                if ((gate & open_bit) == 0 || (gate & reference_mask) >= slot_count_)
                {
                    return false;
                }
                if (gate_.compare_exchange_strong(gate, gate + 1, std::memory_order_acq_rel))
                {
                    admitted = true;
                    break;
                }
            }
            if (!admitted)
            {
                return false;
            }

            // gate의 참조를 보관하는 동안 owner는 current_를 다른 프레임으로 교체하지 못한다.
            auto* frame = current_raw_.load(std::memory_order_acquire);
            if (frame == nullptr || (gate & reference_mask) + 1 > frame->options_.max_workers
                || stopped_.load(std::memory_order_acquire))
            {
                release_reference();
                return false;
            }
            const auto idle_ns = static_cast<std::uint64_t>(frame->options_.idle_budget.count()) * 1000;
            auto remaining = frame->remaining_ns_.load(std::memory_order_relaxed);
            std::uint64_t allowance = 0;
            for (unsigned int attempt = 0; attempt < 8 && remaining != 0; ++attempt)
            {
                const auto requested = (std::min)(idle_ns, remaining);
                if (frame->remaining_ns_.compare_exchange_strong(remaining, remaining - requested,
                                                               std::memory_order_relaxed))
                {
                    allowance = requested;
                    break;
                }
            }
            if (allowance == 0)
            {
                release_reference();
                return false;
            }

            slot.frame_ = frame;
            slot.epoch_ = gate & epoch_mask;
            slot.allowance_ns_ = allowance;
            slot.started_ns_ = clock_();
            frame->reserved_budget_ns_.fetch_add(allowance, std::memory_order_relaxed);
            frame->spin_attempts_.fetch_add(1, std::memory_order_relaxed);
            const auto active = frame->active_spinners_.fetch_add(1, std::memory_order_relaxed) + 1;
            auto peak = frame->max_active_spinners_.load(std::memory_order_relaxed);
            while (peak < active && !frame->max_active_spinners_.compare_exchange_strong(peak, active,
                                                                                       std::memory_order_relaxed))
            {
            }
            return true;
        }

        enki::IdleSpinDecision poll(std::uint32_t thread_id) noexcept
        {
            if (stopped_.load(std::memory_order_acquire))
            {
                return enki::IdleSpinDecision::Shutdown;
            }
            if (thread_id >= slot_count_ || slots_[thread_id].frame_ == nullptr)
            {
                return enki::IdleSpinDecision::ScopeEnded;
            }
            const auto& slot = slots_[thread_id];
            const auto gate = gate_.load(std::memory_order_acquire);
            if ((gate & open_bit) == 0 || (gate & epoch_mask) != slot.epoch_)
            {
                return enki::IdleSpinDecision::ScopeEnded;
            }
            const auto now = clock_();
            return now >= slot.started_ns_ && now - slot.started_ns_ < slot.allowance_ns_
                ? enki::IdleSpinDecision::Continue : enki::IdleSpinDecision::BudgetExhausted;
        }

        void end(std::uint32_t thread_id, enki::IdleSpinResult result) noexcept
        {
            if (thread_id >= slot_count_ || slots_[thread_id].frame_ == nullptr)
            {
                return;
            }
            auto& slot = slots_[thread_id];
            auto* frame = slot.frame_;
            const auto now = clock_();
            const auto elapsed = now >= slot.started_ns_ ? now - slot.started_ns_ : 0;
            slot.frame_ = nullptr;
            frame->spin_wall_ns_.fetch_add(elapsed, std::memory_order_relaxed);
            switch (result)
            {
            case enki::IdleSpinResult::TaskFound:
                frame->task_hits_.fetch_add(1, std::memory_order_relaxed);
                break;
            case enki::IdleSpinResult::BudgetExhausted:
                frame->budget_exhaustions_.fetch_add(1, std::memory_order_relaxed);
                break;
            case enki::IdleSpinResult::ScopeEnded:
                frame->scope_exits_.fetch_add(1, std::memory_order_relaxed);
                break;
            case enki::IdleSpinResult::Shutdown:
                frame->shutdown_exits_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            frame->active_spinners_.fetch_sub(1, std::memory_order_relaxed);
            // 통계와 slot 정리를 끝낸 뒤에만 마지막 참조를 놓는다.
            release_reference();
        }

      private:
        struct worker_slot
        {
            frame_state* frame_{};
            std::uint64_t epoch_{};
            std::uint64_t started_ns_{};
            std::uint64_t allowance_ns_{};
        };

        static constexpr std::uint64_t open_bit = std::uint64_t{1} << 63;
        static constexpr std::uint64_t reference_mask = 0xffffffff;
        static constexpr std::uint64_t epoch_mask = ~(open_bit | reference_mask);
        static constexpr std::uint64_t epoch_step = std::uint64_t{1} << 32;

        bool open_gate() noexcept
        {
            auto gate = gate_.load(std::memory_order_acquire);
            for (unsigned int attempt = 0; attempt < 8; ++attempt)
            {
                if (stopped_.load(std::memory_order_acquire))
                {
                    return false;
                }
                const auto epoch = ((gate & epoch_mask) + epoch_step) & epoch_mask;
                const auto next = epoch | open_bit | (gate & reference_mask);
                if (gate_.compare_exchange_strong(gate, next, std::memory_order_acq_rel))
                {
                    return true;
                }
            }
            return false;
        }

        void release_reference() noexcept
        {
            gate_.fetch_sub(1, std::memory_order_release);
        }

        std::mutex mutex_;
        std::shared_ptr<frame_state> current_;
        std::atomic<frame_state*> current_raw_{};
        // open/epoch와 worker 참조를 하나의 CAS로 다뤄 raw pointer의 획득과 해제를 보호한다.
        std::atomic<std::uint64_t> gate_{};
        std::atomic<bool> stopped_{};
        const std::uint32_t slot_count_;
        std::unique_ptr<worker_slot[]> slots_;
        const clock_function clock_;
    };

    inline void scope_token::close() noexcept
    {
        owner_->close_scope(frame_);
    }
}

// callback의 userData는 공유 state다. pool은 stop→scheduler join 뒤에 이 소유자를 해제한다.
class thread_pool_spin_control
{
  public:
    using clock_function = job_spin_detail::clock_function;

    explicit thread_pool_spin_control(std::uint32_t total_thread_slots,
                                      clock_function clock = &job_spin_detail::steady_now_ns)
        : state_(std::make_shared<job_spin_detail::control_state>(total_thread_slots, clock))
    {
        if (enki::GetCreatorIdleSpinApiVersion() != ENKITS_CREATOR_IDLE_SPIN_API)
        {
            throw std::logic_error("enkiTS scoped spin header/library version mismatch");
        }
    }

    thread_pool_spin_control(const thread_pool_spin_control&) = delete;
    thread_pool_spin_control& operator=(const thread_pool_spin_control&) = delete;

    ~thread_pool_spin_control()
    {
        stop();
    }

    enki::IdleSpinCallbacks callbacks() const noexcept
    {
        enki::IdleSpinCallbacks value;
        value.userData = state_.get();
        value.begin = [](void* user_data, std::uint32_t thread_id) noexcept
        {
            return static_cast<job_spin_detail::control_state*>(user_data)->begin(thread_id);
        };
        value.poll = [](void* user_data, std::uint32_t thread_id) noexcept
        {
            return static_cast<job_spin_detail::control_state*>(user_data)->poll(thread_id);
        };
        value.end = [](void* user_data, std::uint32_t thread_id, enki::IdleSpinResult result) noexcept
        {
            static_cast<job_spin_detail::control_state*>(user_data)->end(thread_id, result);
        };
        return value;
    }

    job_spin_scope begin_scope(job_spin_options options)
    {
        return job_spin_scope(state_->begin_scope(options));
    }

    void stop() noexcept
    {
        state_->stop();
    }

  private:
    std::shared_ptr<job_spin_detail::control_state> state_;
};
