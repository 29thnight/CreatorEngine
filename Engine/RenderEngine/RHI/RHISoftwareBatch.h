#pragma once
#include "RHISoftwareEncoder.h"
#include "RHIRecordedBatch.h"
#include "RHIQueueContract.h"

class RHISoftwareRecorder;
class RHICommandTranslator;

struct RHISoftwareRecordingStorage
{
    struct Target
    {
        ce::render_vector<ce::render_unique_ptr<RHISoftwareCommandList>> segments;
        size_t activeSegments{};
    };
    ce::render_vector<Target> targets;
    ce::render_vector<uint32_t> targetOrder;
    ce::render_vector<std::shared_ptr<RHISoftwareRecordingBudget>> budgets;
    ce::render_vector<ce::render_unique_ptr<RHISoftwareEncoder>> encoders;
    void Reset() noexcept;
    size_t RetainedBytes() const noexcept;
};

struct RHISoftwareBatchDesc
{
    uint64_t frameId{};
    uint32_t sourceEngineFrame{};
    bool sourceEngineFrameAvailable{};
    uint64_t renderViewId{};
    uint64_t backendGeneration{};
    uint64_t displayToken{};
    uint64_t uploadRecordingId{};
    uint64_t descriptorVersion{};
    RHIQueueIdentity queue;
#if !CE_SHIPPING && CE_DX_TIMING_CAPTURE
    ce::dx_capture::submission_context captureContext{};
#endif
    std::shared_ptr<const void> lifetimeToken;
};

// Software state is intentionally distinct from RHIRecordedBatch's native state.
class RHISoftwareBatch final
{
  public:
    RHISoftwareBatch() = default;
    RHISoftwareBatch(const RHISoftwareBatch&) = delete;
    RHISoftwareBatch& operator=(const RHISoftwareBatch&) = delete;
    RHISoftwareBatch(RHISoftwareBatch&& other) noexcept;
    RHISoftwareBatch& operator=(RHISoftwareBatch&& other) noexcept;

    bool IsValid() const { return m_state == RHISoftwareBatchState::Sealed; }
    RHISoftwareBatchState GetState() const { return m_state; }
    const RHISoftwareBatchDesc& GetDesc() const { return m_desc; }
    uint64_t GetRecordingId() const { return m_recordingId; }
    uint32_t GetTargetCount() const { return m_storage ? static_cast<uint32_t>(m_storage->targetOrder.size()) : 0; }
    const RHICommandRecordingStats& GetRecordingStats() const { return m_stats; }

  private:
    friend class RHISoftwareRecorder;
    friend class RHICommandTranslator;
    void MoveFrom(RHISoftwareBatch&& other) noexcept;

    std::shared_ptr<RHISoftwareRecordingStorage> m_storage;
    RHISoftwareBatchDesc m_desc;
    uint64_t m_recordingId{};
    RHISoftwareBatchState m_state{RHISoftwareBatchState::Empty};
    RHICommandRecordingStats m_stats;
};
