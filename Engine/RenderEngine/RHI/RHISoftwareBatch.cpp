#include "RHISoftwareBatch.h"

RHISoftwareBatch::RHISoftwareBatch(RHISoftwareBatch&& other) noexcept
{
    MoveFrom(std::move(other));
}

RHISoftwareBatch& RHISoftwareBatch::operator=(RHISoftwareBatch&& other) noexcept
{
    if (this != &other)
    {
        MoveFrom(std::move(other));
    }
    return *this;
}

void RHISoftwareBatch::MoveFrom(RHISoftwareBatch&& other) noexcept
{
    m_targets = std::move(other.m_targets);
    m_targetOrder = std::move(other.m_targetOrder);
    m_desc = std::move(other.m_desc);
    m_recordingId = std::exchange(other.m_recordingId, 0);
    m_state = std::exchange(other.m_state, RHISoftwareBatchState::Empty);
    m_stats = std::exchange(other.m_stats, {});
}
