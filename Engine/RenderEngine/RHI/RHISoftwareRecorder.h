#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
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
    uint64_t GetRecordingId() const { return m_recordingId; }
    bool HasRecorded(uint32_t target) const
    {
        return m_storage && target < m_storage->activeTargetCount && m_storage->targets[target].activeSegments != 0;
    }
    RHICommandRecordingStats GetRecordingStats() const;

  private:
    const IRHISoftwareCommandValidator& m_validator;
    ce::pooled_owner<RHISoftwareRecordingStorage> m_storage;
    RHISoftwareRecordingDesc m_desc;
    uint64_t m_recordingId{};
    bool m_active{};
};
