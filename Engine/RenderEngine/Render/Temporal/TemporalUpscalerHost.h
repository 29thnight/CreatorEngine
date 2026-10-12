#pragma once
#include "ITemporalUpscaler.h"
#include "TemporalRuntimeControl.h"
#include <memory>
class IRHIDeviceResources;

// A settings acknowledgement is not a reconstruction reconfiguration. Reflex,
// FG and display color/sharpness controls do not invalidate temporal history.
inline bool SameTemporalReconstructionSettings(const TemporalRuntimeSettings& left,
    const TemporalRuntimeSettings& right)
{
    const auto leftProvider = left.enabled ? left.requestedUpscaler : TemporalProvider::None;
    const auto rightProvider = right.enabled ? right.requestedUpscaler : TemporalProvider::None;
    if (leftProvider != rightProvider || left.testFault != right.testFault)
    {
        return false;
    }
    if (leftProvider == TemporalProvider::None)
    {
        return true;
    }
    return left.quality == right.quality && left.runtimeDirectory == right.runtimeDirectory &&
        left.dlssProjectId == right.dlssProjectId;
}

// One owner per logical view. Configure is called before graph resource sizing.
// A failed SDK recording invalidates that frame; the caller must not publish its
// unwritten output. The following Configure selects FSR or full native extent.
// Retry a faulted provider with a reconstruction change or explicit test-fault revision.
class TemporalUpscalerHost
{
public:
    TemporalUpscalerHost();
    ~TemporalUpscalerHost();
    TemporalUpscalerHost(const TemporalUpscalerHost&) = delete;
    TemporalUpscalerHost& operator=(const TemporalUpscalerHost&) = delete;
    TemporalResult Configure(IRHIDeviceResources&, TemporalBackend,
        const TemporalRuntimeSettings&, uint64_t generation, TemporalExtent displayExtent,
        bool depthInverted = false, bool orthographic = false, uint64_t viewId = 0, uint64_t sceneEpoch = 0);
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
