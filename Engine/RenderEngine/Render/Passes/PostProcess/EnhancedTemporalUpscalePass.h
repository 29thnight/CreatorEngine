#pragma once
#include "../../Graph/EnhancedRenderPass.h"
#include "../../Temporal/TemporalUpscalerHost.h"

// One vendor-neutral HDR reconstruction pass. The per-view host owns SDK
// selection/history; the graph owns resource transitions and output lifetime.
class EnhancedTemporalUpscalePass final : public EnhancedRenderPass
{
public:
    const char* GetName() const override { return "Temporal.Upscale"; }
    bool Initialize(const EnhancedFrameContext&, std::string&) override { return true; }
    void SetInputs(TemporalUpscalerHost& host, const std::array<RGHandle, 6>& inputs,
        bool spatialScales, bool& applied, float exposure = 1.f)
    { m_host = &host; m_inputs = inputs; m_spatialScales = spatialScales; m_applied = &applied; m_exposure = exposure; }
    void Declare(EnhancedRenderGraph&, const EnhancedFrameContext&) override;
    RGHandle GetOutput() const { return m_output; }
private:
    TemporalUpscalerHost* m_host{};
    std::array<RGHandle, 6> m_inputs{};
    bool m_spatialScales{};
    bool* m_applied{};
    float m_exposure{1.f};
    RGHandle m_output;
};
