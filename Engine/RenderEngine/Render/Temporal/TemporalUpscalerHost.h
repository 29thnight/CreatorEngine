#pragma once
#include "ITemporalUpscaler.h"
#include "TemporalRuntimeControl.h"
#include <memory>
class IRHIDeviceResources;

// One owner per logical view. Configure is called before graph resource sizing.
// A failed SDK recording invalidates that frame; the caller must not publish its
// unwritten output. The following Configure selects FSR or full native extent.
class TemporalUpscalerHost
{
public:
    TemporalUpscalerHost();
    ~TemporalUpscalerHost();
    TemporalUpscalerHost(const TemporalUpscalerHost&) = delete;
    TemporalUpscalerHost& operator=(const TemporalUpscalerHost&) = delete;
    TemporalResult Configure(IRHIDeviceResources&, TemporalBackend,
        const TemporalRuntimeSettings&, uint64_t generation, TemporalExtent displayExtent,
        bool depthInverted = false, bool orthographic = false);
    TemporalExtent RenderExtent() const;
    TemporalProvider Provider() const;
    TemporalResult LastResult() const;
    TemporalResult RequestedResult() const;
    const std::vector<TemporalCapabilities>& Capabilities() const;
    TemporalResult Evaluate(const TemporalUpscaleInputs&, RHIEncoder&);
    TemporalResult Shutdown();
private:
    struct State;
    std::unique_ptr<State> m_state;
};
