#pragma once

#include "../../Utility_Framework/Ownership.h"
#include "../../Utility_Framework/JobScheduler.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

class DataSystem;

namespace AssetDepot
{
    // CPU readiness only. Ready does not imply a GPU upload or completed fence.
    enum class AssetRequestStatus : std::uint8_t
    {
        Pending,
        Ready,
        Failed,
        Cancelled,
        Stale,
    };

    enum class AssetRequestError : std::uint8_t
    {
        None,
        InvalidLink,
        NotMounted,
        TypeMismatch,
        HardDependencyCycle,
        UnsupportedType,
        UnsupportedRepresentation,
        ReadFailed,
        IntegrityFailed,
        DecodeFailed,
        DependencyFailed,
        SubmissionFailed,
        ShuttingDown,
        RevisionChanged,
    };

    struct AssetRequestStatistics final
    {
        std::uint64_t created{};
        std::uint64_t liveConsumers{};
        std::uint64_t pendingConsumers{};
        std::uint64_t ready{};
        std::uint64_t failed{};
        std::uint64_t cancelled{};
        std::uint64_t stale{};
        std::uint64_t abandonedPending{};
    };

    // One counter value owned by DataSystem and its independent consumer states.
    // It outlives service teardown when a request handle does. Cumulative outcomes
    // count consumers, not joined jobs or handle copies; abandoned Pending is not
    // an explicit cancellation. Atomic field reads are observational, not one
    // transactional snapshot, and never decide lifetime or publication safety.
    struct AssetRequestCounters final
    {
        std::atomic<std::uint64_t> created{}, liveConsumers{}, pendingConsumers{};
        std::atomic<std::uint64_t> ready{}, failed{}, cancelled{}, stale{}, abandonedPending{};

        [[nodiscard]] AssetRequestStatistics Snapshot() const noexcept
        {
            return { created.load(std::memory_order_relaxed), liveConsumers.load(std::memory_order_relaxed),
                pendingConsumers.load(std::memory_order_relaxed), ready.load(std::memory_order_relaxed),
                failed.load(std::memory_order_relaxed), cancelled.load(std::memory_order_relaxed),
                stale.load(std::memory_order_relaxed), abandonedPending.load(std::memory_order_relaxed) };
        }
    };

    // Tracks observed vector capacity or decoded result bytes in one accepted
    // worker scope. The tracked-job shutdown barrier keeps the containing service
    // counter alive. Declare this before the owned buffer so destruction removes
    // bytes only after that buffer is released. SDK-internal temporary allocations
    // and allocator overhead are deliberately outside this measurement.
    class AssetByteObservation final
    {
    public:
        explicit AssetByteObservation(std::atomic<std::size_t>& total) noexcept : m_total(total) {}
        AssetByteObservation(const AssetByteObservation&) = delete;
        AssetByteObservation& operator=(const AssetByteObservation&) = delete;
        ~AssetByteObservation() { m_total.fetch_sub(m_bytes, std::memory_order_relaxed); }

        void Set(std::size_t bytes) noexcept
        {
            if (bytes >= m_bytes)
            {
                m_total.fetch_add(bytes - m_bytes, std::memory_order_relaxed);
            }
            else
            {
                m_total.fetch_sub(m_bytes - bytes, std::memory_order_relaxed);
            }
            m_bytes = bytes;
        }

    private:
        std::atomic<std::size_t>& m_total;
        std::size_t m_bytes{};
    };

    template<class T>
    struct AssetRequestSnapshot final
    {
        AssetRequestStatus status{ AssetRequestStatus::Failed };
        AssetRequestError error{ AssetRequestError::InvalidLink };
        std::string message{};
        own::shared_owner<const T> asset{};
    };

    // Mutable consumer state, not the shared generation job. All handle copies
    // refer to this consumer; a separate RequestAsync call gets separate state.
    template<class T>
    struct AssetRequestState final
    {
        explicit AssetRequestState(own::shared_owner<AssetRequestCounters> statistics = {})
            : counters(std::move(statistics))
        {
            if (counters)
            {
                counters->created.fetch_add(1u, std::memory_order_relaxed);
                counters->liveConsumers.fetch_add(1u, std::memory_order_relaxed);
                counters->pendingConsumers.fetch_add(1u, std::memory_order_relaxed);
            }
        }

        ~AssetRequestState()
        {
            if (counters)
            {
                if (status == AssetRequestStatus::Pending)
                {
                    counters->pendingConsumers.fetch_sub(1u, std::memory_order_relaxed);
                    counters->abandonedPending.fetch_add(1u, std::memory_order_relaxed);
                }
                counters->liveConsumers.fetch_sub(1u, std::memory_order_relaxed);
            }
        }

        // Caller holds mutex. Count exactly the first terminal consumer result.
        void SetTerminalLocked(AssetRequestStatus terminal) noexcept
        {
            if (status != AssetRequestStatus::Pending || terminal == AssetRequestStatus::Pending)
            {
                return;
            }
            status = terminal;
            if (!counters)
            {
                return;
            }
            counters->pendingConsumers.fetch_sub(1u, std::memory_order_relaxed);
            switch (terminal)
            {
            case AssetRequestStatus::Ready:
                counters->ready.fetch_add(1u, std::memory_order_relaxed);
                break;
            case AssetRequestStatus::Failed:
                counters->failed.fetch_add(1u, std::memory_order_relaxed);
                break;
            case AssetRequestStatus::Cancelled:
                counters->cancelled.fetch_add(1u, std::memory_order_relaxed);
                break;
            case AssetRequestStatus::Stale:
                counters->stale.fetch_add(1u, std::memory_order_relaxed);
                break;
            default:
                break;
            }
        }

        const own::shared_owner<AssetRequestCounters> counters;
        mutable std::mutex mutex{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const T> asset{};
        job_handle completion{};
    };

    template<class T>
    class AssetRequest final
    {
    public:
        AssetRequest() = default;

        [[nodiscard]] AssetRequestSnapshot<T> Snapshot() const
        {
            if (!m_state)
            {
                return {};
            }
            std::lock_guard lock(m_state->mutex);
            return { m_state->status, m_state->error, m_state->message, m_state->asset };
        }

        // Cancelling one subscriber neither cancels a joined generation job nor
        // invalidates another subscriber. Work/captures still drain normally.
        void Cancel() const
        {
            if (!m_state)
            {
                return;
            }
            std::lock_guard lock(m_state->mutex);
            if (m_state->status == AssetRequestStatus::Pending)
            {
                m_state->SetTerminalLocked(AssetRequestStatus::Cancelled);
                m_state->error = AssetRequestError::None;
                m_state->message.clear();
                m_state->asset.reset();
            }
        }

        // Scheduler dependency token, not permission to block a GT/RT or worker.
        // Cancellation can be observed before this shared work token completes.
        [[nodiscard]] job_handle Completion() const
        {
            if (!m_state)
            {
                return {};
            }
            std::lock_guard lock(m_state->mutex);
            return m_state->completion;
        }

    private:
        friend class ::DataSystem;
        explicit AssetRequest(own::shared_owner<AssetRequestState<T>> state)
            : m_state(std::move(state))
        {
        }

        own::shared_owner<AssetRequestState<T>> m_state{};
    };
}
