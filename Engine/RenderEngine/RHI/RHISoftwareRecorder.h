#pragma once
#include "RHISoftwareBatch.h"

struct RHISoftwareRecordingDesc
{
    RHIQueueIdentity queue;
    uint32_t targetCount{};
    uint64_t maxCommandBytes{};
    uint64_t maxPayloadBytes{};
};

class RHISoftwareRecorder final
{
  public:
    explicit RHISoftwareRecorder(const IRHISoftwareCommandValidator& validator) : m_validator(validator) {}
    bool BeginRecording(const RHISoftwareRecordingDesc& desc, std::string& outError);
    RHIEncoder& AcquireEncoder(uint32_t target);
    bool FinishRecording(std::span<const uint32_t> targetOrder, const RHISoftwareBatchDesc& desc,
                         RHISoftwareBatch& outBatch, std::string& outError);
    void CancelRecording();
    bool HasRecorded(uint32_t target) const { return target < m_targets.size() && !m_targets[target].segments.empty(); }
    RHICommandRecordingStats GetRecordingStats() const;

  private:
    const IRHISoftwareCommandValidator& m_validator;
    ce::render_vector<std::shared_ptr<RHISoftwareRecordingBudget>> m_budgets;
    RHISoftwareRecordingDesc m_desc;
    ce::render_vector<RHISoftwareBatch::Target> m_targets;
    ce::render_vector<ce::render_unique_ptr<RHISoftwareEncoder>> m_encoders;
    bool m_active{};
};
