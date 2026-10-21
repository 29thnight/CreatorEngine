#include "RHISoftwareBatch.h"

bool RHISoftwareBatch::ReplaySerial(RHIEncoder& encoder, RHICommandFailure& failure)
{
    failure.Reset();
    if (!IsValid() || !m_storage || m_storage->targetOrder.size() > 1)
    {
        failure.message = "Immediate replay requires a sealed single-target software batch.";
        return false;
    }
    m_state = RHISoftwareBatchState::Translating;
    for (const auto targetIndex : m_storage->targetOrder)
    {
        const auto& target = m_storage->targets[targetIndex];
        for (size_t index = 0; index < target.activeSegments; ++index)
        {
            RHICommandTranslationStats stats;
            if (!target.segments[index]->Replay(encoder, stats, failure))
            {
                m_state = RHISoftwareBatchState::Failed;
                return false;
            }
        }
    }
    m_state = RHISoftwareBatchState::Translated;
    return true;
}

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
    m_storage = std::move(other.m_storage);
    m_desc = std::move(other.m_desc);
    m_recordingId = std::exchange(other.m_recordingId, 0);
    m_state = std::exchange(other.m_state, RHISoftwareBatchState::Empty);
    m_stats = std::exchange(other.m_stats, {});
}
