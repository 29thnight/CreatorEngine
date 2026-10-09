#pragma once

#include "TemporalReconstruction.h"

class RHIEncoder;

// Backend-private factories create these with the active SDK/device context.
// Callers do not include vendor headers. This is a render-graph recording
// operation, not queue submission; existing RHI ownership remains authoritative.
class ITemporalUpscaler
{
public:
    virtual ~ITemporalUpscaler() = default;
    virtual TemporalCapabilities GetCapabilities() const = 0;
    // Inputs must already be in RHIResourceState::ShaderResource, output in UnorderedAccess.
    // Use the graph callback's encoder, never a frame-global command list.
    // The SDK can invalidate encoder binding caches; implementations reset them.
    virtual TemporalResult Evaluate(const TemporalUpscaleInputs& inputs, RHIEncoder& encoder) = 0;
    // The private factory supplies the real GPU-drain operation. Shutdown must
    // fail rather than destroy an in-flight context if draining fails.
    virtual TemporalResult Shutdown() = 0;
};
