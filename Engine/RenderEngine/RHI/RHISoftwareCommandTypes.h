#pragma once
#include "RHIEncoder.h"

enum class RHISoftwareBatchState : uint8_t
{
    Empty,
    Sealed,
    Queued,
    Translating,
    Translated,
    Failed,
    Cancelled
};

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

struct RHICommandRecordingStats
{
    uint64_t commandCount{};
    uint64_t commandBytes{};
    uint64_t payloadBytes{};
    uint64_t allocatedBytes{};
    uint64_t arenaBlocks{};
    uint64_t arenaCacheHits{}; // Retained arena blocks activated without a new backing allocation.
    uint64_t computeDispatchCommands{};
};

struct RHICommandTranslationStats
{
    uint32_t translationJobCount{};
    uint64_t commandCount{};
    uint64_t nativeComputeDispatches{};
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

// A device snapshot validates bool-returning operations without emitting native
// commands. It must include resource bounds, capabilities and pipeline state.
// No default accept-all validator is provided for production recording.
struct RHISoftwareEncoderState
{
    RHIPipelineHandle boundPipeline;
    RHIBindPoint bindPoint{RHIBindPoint::Graphics};
};

class IRHISoftwareCommandValidator
{
  public:
    virtual ~IRHISoftwareCommandValidator() = default;
    virtual bool Validate(RHISoftwareCommandType type, const void* arguments, size_t argumentBytes,
                          const RHISoftwareEncoderState& state) const = 0;
};
