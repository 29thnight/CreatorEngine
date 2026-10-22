#pragma once
#include "../../../Utility_Framework/Ownership.h"
#include "SpatialPostEffects.h"
#include "../Graph/EnhancedRenderGraph.h"
#include "../../../Utility_Framework/StandardLibrary.h"

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
    own::unique_owner<State> m_state;
};
