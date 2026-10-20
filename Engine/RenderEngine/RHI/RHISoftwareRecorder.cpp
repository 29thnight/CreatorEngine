#include "RHISoftwareRecorder.h"

namespace
{
    std::atomic<uint64_t> g_softwareRecordingId{1};

    void AddSoftwareRecordingStats(RHICommandRecordingStats& total, const RHICommandRecordingStats& next)
    {
        total.commandCount += next.commandCount;
        total.commandBytes += next.commandBytes;
        total.payloadBytes += next.payloadBytes;
        total.allocatedBytes += next.allocatedBytes;
        total.arenaBlocks += next.arenaBlocks;
        total.arenaCacheHits += next.arenaCacheHits;
        total.computeDispatchCommands += next.computeDispatchCommands;
    }
} // namespace

bool RHISoftwareRecorder::BeginRecording(const RHISoftwareRecordingDesc& desc, std::string& outError)
{
    outError.clear();
    if (m_active || !desc.queue.IsValid() || !desc.targetCount || desc.targetCount > 8 ||
        desc.maxCommandBytes < desc.targetCount || desc.maxPayloadBytes < desc.targetCount)
    {
        outError =
            "Software recording requires an inactive session, identified queue, 1..8 targets and positive budgets.";
        return false;
    }
    m_targets.clear();
    m_encoders.clear();
    m_targets.resize(desc.targetCount);
    m_encoders.resize(desc.targetCount);
    m_budgets.clear();
    m_budgets.reserve(desc.targetCount);
    for (uint32_t target = 0; target < desc.targetCount; ++target)
    {
        auto budget = ce::make_render_shared<RHISoftwareRecordingBudget>();
        budget->maxCommandBytes =
            desc.maxCommandBytes / desc.targetCount + (target < desc.maxCommandBytes % desc.targetCount ? 1 : 0);
        budget->maxPayloadBytes =
            desc.maxPayloadBytes / desc.targetCount + (target < desc.maxPayloadBytes % desc.targetCount ? 1 : 0);
        m_budgets.push_back(std::move(budget));
    }
    m_desc = desc;
    m_active = true;
    return true;
}

RHIEncoder& RHISoftwareRecorder::AcquireEncoder(uint32_t target)
{
    if (!m_active || target >= m_targets.size())
    {
        throw std::out_of_range("Software encoder target is outside the active session.");
    }
    auto& segments = m_targets[target].segments;
    if (!segments.empty())
    {
        // Seal clears this on success. Keep the diagnostic string's Debug STL
        // proxy across passes instead of allocating/orphaning one per pass.
        thread_local std::string error;
        if (!segments.back()->Seal(error))
        {
            throw std::runtime_error(error);
        }
    }
    auto list = ce::make_render_unique<RHISoftwareCommandList>(m_desc.maxCommandBytes, m_desc.maxPayloadBytes,
                                                               m_budgets[target]);
    if (!m_encoders[target])
    {
        m_encoders[target] = ce::make_render_unique<RHISoftwareEncoder>(*list, m_validator);
    }
    else
    {
        m_encoders[target]->Reset(*list);
    }
    segments.push_back(std::move(list));
    return *m_encoders[target];
}

RHICommandRecordingStats RHISoftwareRecorder::GetRecordingStats() const
{
    RHICommandRecordingStats total;
    for (const auto& target : std::span(m_targets.data(), m_targets.size()))
    {
        for (const auto& segment : std::span(target.segments.data(), target.segments.size()))
        {
            AddSoftwareRecordingStats(total, segment->GetRecordingStats());
        }
    }
    return total;
}

bool RHISoftwareRecorder::FinishRecording(std::span<const uint32_t> targetOrder, const RHISoftwareBatchDesc& desc,
                                          RHISoftwareBatch& outBatch, std::string& outError)
{
    outError.clear();
    if (!m_active || outBatch.GetState() != RHISoftwareBatchState::Empty || desc.queue != m_desc.queue ||
        !desc.backendGeneration || !desc.lifetimeToken)
    {
        outError = "Software batch output, queue identity, backend generation or lifetime owner is invalid.";
        return false;
    }
    std::bitset<8> seen;
    for (const uint32_t target : targetOrder)
    {
        if (target >= m_targets.size() || seen.test(target) || m_targets[target].segments.empty())
        {
            outError = "Software target order has an invalid, duplicate or unopened target.";
            return false;
        }
        seen.set(target);
    }
    for (size_t target = 0; target < m_targets.size(); ++target)
    {
        for (const auto& segment : std::span(m_targets[target].segments.data(), m_targets[target].segments.size()))
        {
            if (segment->GetCommandCount() && !seen.test(target))
            {
                outError = "Software target order omitted recorded commands.";
                return false;
            }
        }
    }
    const auto stats = GetRecordingStats();
    if (stats.commandBytes > m_desc.maxCommandBytes || stats.payloadBytes > m_desc.maxPayloadBytes)
    {
        outError = "Software recording exceeded its aggregate byte budget.";
        return false;
    }
    for (auto& target : std::span(m_targets.data(), m_targets.size()))
    {
        for (auto& segment : std::span(target.segments.data(), target.segments.size()))
        {
            if ((!segment->IsSealed() && !segment->Seal(outError)) || !segment->GetError().empty())
            {
                if (outError.empty())
                {
                    outError = segment->GetError();
                }
                return false;
            }
        }
    }
    RHISoftwareBatch batch;
    batch.m_targetOrder.assign(targetOrder.begin(), targetOrder.end());
    batch.m_desc = desc;
    batch.m_stats = stats;
    batch.m_recordingId = g_softwareRecordingId.fetch_add(1, std::memory_order_relaxed);
    batch.m_targets = std::move(m_targets);
    batch.m_state = RHISoftwareBatchState::Sealed;
    m_encoders.clear();
    m_budgets.clear();
    m_active = false;
    outBatch = std::move(batch);
    return true;
}

void RHISoftwareRecorder::CancelRecording()
{
    m_encoders.clear();
    m_targets.clear();
    m_budgets.clear();
    m_active = false;
}
