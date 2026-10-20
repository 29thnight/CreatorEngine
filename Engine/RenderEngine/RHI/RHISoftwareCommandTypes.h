#pragma once
#include "RHIEncoder.h"
#include "RHICommandFailure.h"

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

// A device snapshot validates bool-returning operations without emitting native
// commands. It must include resource bounds, capabilities and pipeline state.
// No default accept-all validator is provided for production recording.
struct RHISoftwareEncoderState
{
    std::array<RHIPipelineHandle, 2> pipelines;
    RHIPipelineHandle boundPipeline;
    RHIBindPoint bindPoint{RHIBindPoint::Graphics};
    RHIRenderTargetBinding renderTargets;
    bool renderingOpen{};
};

class IRHISoftwareCommandValidator
{
  public:
    virtual ~IRHISoftwareCommandValidator() = default;
    virtual bool Validate(RHISoftwareCommandType type, const void* arguments, size_t argumentBytes,
                          const RHISoftwareEncoderState& state) const = 0;

    // Native backends differ in which commands end a raster scope. Apply only
    // after command admission; a rejected bool command never changes state.
    virtual void ApplyRecordedState(RHISoftwareCommandType, const void*, size_t, RHISoftwareEncoderState&) const {}
};
