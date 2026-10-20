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
    bool HasRecorded(uint32_t target) const
    {
        return m_storage && target < m_storage->targets.size() && m_storage->targets[target].activeSegments != 0;
    }
    RHICommandRecordingStats GetRecordingStats() const;

  private:
    const IRHISoftwareCommandValidator& m_validator;
    std::shared_ptr<RHISoftwareRecordingStorage> m_storage;
    RHISoftwareRecordingDesc m_desc;
    bool m_active{};
};
