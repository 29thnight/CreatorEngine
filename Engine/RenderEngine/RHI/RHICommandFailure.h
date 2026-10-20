#pragma once
#include "../../Utility_Framework/StandardLibrary.h"

enum class RHISoftwareCommandType : uint8_t
{
    SetViewportAndScissor,
    SetViewport,
    SetScissor,
    SetPipeline,
    SetPrimitiveTopology,
    SetBindings,
    SetSamplers,
    SetConstantBuffer,
    SetRootBuffer,
    SetRootUnorderedBuffer,
    SetVertexBuffer,
    SetIndexBuffer,
    Draw,
    DrawIndexed,
    DrawIndirect,
    DrawIndexedIndirect,
    DrawIndexedIndirectBatch,
    Dispatch,
    DispatchIndirect,
    DispatchMesh,
    DispatchMeshIndirect,
    DispatchMeshIndirectBatch,
    BeginRasterWithoutAttachments,
    BindRenderTargets,
    ClearRenderTargets,
    ClearDepthTarget,
    ClearRenderTargetRect,
    ResourceBarriers,
    UavBarrier,
    UavBarrierBuffers,
    CopyResource,
    CopyTexture,
    CopyBuffer,
    ClearUnorderedAccess,
    CopyToReadback,
    CopyVolumeToReadback,
    CopyPartialToReadback,
    CopyBufferToReadback,
    BeginGpuPass,
    EndGpuPass,
    ResolveGpuQueries,
    BeginDebugEvent,
    EndDebugEvent,
    Count
};

enum class RHICommandFailureStage : uint8_t
{
    Recording,
    Admission,
    Translation,
    Submission,
    Retirement
};

struct RHICommandFailure
{
    RHICommandFailureStage stage{RHICommandFailureStage::Recording};
    uint64_t recordingId{};
    uint32_t targetIndex{UINT32_MAX};
    uint64_t commandIndex{UINT64_MAX};
    RHISoftwareCommandType commandType{RHISoftwareCommandType::Count};
    uint64_t backendGeneration{};
    bool executionUncertain{};
    std::string message;

    void CopyContextFrom(const RHICommandFailure& failure) noexcept
    {
        stage = failure.stage;
        recordingId = failure.recordingId;
        targetIndex = failure.targetIndex;
        commandIndex = failure.commandIndex;
        commandType = failure.commandType;
        backendGeneration = failure.backendGeneration;
        executionUncertain = failure.executionUncertain;
    }

    void Reset()
    {
        stage = RHICommandFailureStage::Recording;
        recordingId = 0;
        targetIndex = UINT32_MAX;
        commandIndex = UINT64_MAX;
        commandType = RHISoftwareCommandType::Count;
        backendGeneration = 0;
        executionUncertain = false;
        message.clear();
    }
};

// A request-scoped diagnostic sink follows synchronous submission-owner handoffs.
// Workers report through their joined result slots; they never borrow this pointer.
class RHICommandFailureScope final
{
  public:
    explicit RHICommandFailureScope(RHICommandFailure* failure, bool forwardContext = false) noexcept
        : m_failure(failure), m_previous(m_current), m_forwardContext(forwardContext)
    {
        m_current = failure;
    }
    explicit RHICommandFailureScope(RHICommandFailure& failure) noexcept : RHICommandFailureScope(&failure) {}
    ~RHICommandFailureScope()
    {
        // Synchronous owner handoffs preserve both the caller's result and the
        // owner's ticket. Its normal completion path supplies the message.
        if (m_forwardContext && m_failure && m_previous && m_failure != m_previous)
        {
            m_previous->CopyContextFrom(*m_failure);
        }
        m_current = m_previous;
    }
    RHICommandFailureScope(const RHICommandFailureScope&) = delete;
    RHICommandFailureScope& operator=(const RHICommandFailureScope&) = delete;

    static RHICommandFailure* Current() noexcept { return m_current; }
    static void Report(const RHICommandFailure& failure)
    {
        if (m_current)
        {
            *m_current = failure;
        }
    }
    static void SetStage(RHICommandFailureStage stage) noexcept
    {
        if (m_current)
        {
            m_current->stage = stage;
        }
    }
    static void SetIdentity(uint64_t recordingId, uint64_t backendGeneration) noexcept
    {
        if (m_current)
        {
            m_current->recordingId = recordingId;
            m_current->backendGeneration = backendGeneration;
        }
    }
    // Set immediately before the native execute/submit call; clear only once its
    // completion signal is accepted. Preflight rejection never enters this state.
    static void SetExecutionUncertain(bool uncertain) noexcept
    {
        if (m_current)
        {
            m_current->stage = RHICommandFailureStage::Submission;
            m_current->executionUncertain = uncertain;
        }
    }

  private:
    inline static thread_local RHICommandFailure* m_current{};
    RHICommandFailure* m_failure{};
    RHICommandFailure* m_previous{};
    bool m_forwardContext{};
};
