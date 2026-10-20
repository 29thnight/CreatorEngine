#pragma once

#include "RHIQueueContract.h"

// Value-only recording identity retained with native query slots and delayed
// results. It never owns resources or substitutes collection-time frame state.
struct RHICommandProvenance
{
    uint64_t recordingId{};
    uint64_t sourceEngineFrame{};
    uint64_t frameId{};
    uint64_t renderViewId{};
    RHIQueueIdentity queue;
    uint32_t targetIndex{UINT32_MAX};
    uint64_t backendGeneration{};
    bool sourceEngineFrameAvailable{};
};
