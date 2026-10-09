#pragma once

#include "TemporalReconstruction.h"

class RHIEncoder;

// Created only by the shell's backend-private proxy-swapchain factory. Upscaler
// selection is independent. Native presenter setup and completion fences never
// cross this public boundary.
class ITemporalFrameGenerator
{
public:
    virtual ~ITemporalFrameGenerator() = default;
    virtual TemporalCapabilities GetCapabilities() const = 0;
    virtual TemporalResult Prepare(const TemporalFrameGenerationInputs& inputs, RHIEncoder& encoder) = 0;
    // Prepare records/tags SDK work; it is NOT permission to release resources.
    // Present and SDK final-consumption completion belong to the shell adapter.
    virtual TemporalResult Shutdown() = 0;
};
