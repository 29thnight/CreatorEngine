#pragma once

#include "../../Utility_Framework/Ownership.h"
#include "../../Utility_Framework/JobScheduler.h"

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
                m_state->status = AssetRequestStatus::Cancelled;
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
