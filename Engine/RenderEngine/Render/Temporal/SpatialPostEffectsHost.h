#pragma once
#include "SpatialPostEffects.h"
#include "../Graph/EnhancedRenderGraph.h"
#include <memory>

class IRHIDeviceResources;
struct EnhancedFrameContext;
struct TemporalRuntimeSettings;

// Per-view optional SDR effects. SDK ownership stays in the RHI adapter; the
// graph retains the real input/output textures until GPU submission completes.
class SpatialPostEffectsHost
{
public:
    SpatialPostEffectsHost();
    ~SpatialPostEffectsHost();
    SpatialPostEffectsHost(const SpatialPostEffectsHost&) = delete;
    SpatialPostEffectsHost& operator=(const SpatialPostEffectsHost&) = delete;

    TemporalResult Configure(IRHIDeviceResources&, TemporalBackend,
        const TemporalRuntimeSettings&, uint64_t generation, TemporalExtent display,
        bool temporalResolved, bool toneMappedSdr, bool nativeOnly);
    TemporalExtent RenderExtent(TemporalExtent temporalExtent) const;
    bool Scales() const;
    RGHandle Declare(EnhancedRenderGraph&, const EnhancedFrameContext&, RGHandle input);
    SpatialPostSnapshot Snapshot() const;
    TemporalResult Shutdown();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
