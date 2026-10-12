#pragma once
#include "../../Graph/EnhancedRenderPass.h"

// Initializes camera/sky motion and the independent material masks. Geometry
// producers overlay this at the same render extent, before any upscaling/UI.
class EnhancedTemporalInputsPass final : public EnhancedRenderPass
{
public:
    const char* GetName() const override { return "Temporal.Inputs"; }
    bool Initialize(const EnhancedFrameContext&, std::string&) override;
    void SetDepth(RGHandle depth) { m_depth = depth; }
    void Declare(EnhancedRenderGraph&, const EnhancedFrameContext&) override;
    const std::array<RGHandle,5>& Outputs() const { return m_outputs; }
private:
    RHIPipelineHandle m_pipeline;
    RGHandle m_depth;
    std::array<RGHandle,5> m_outputs{}; // motion/reactive/transparency/responsive/depth
};
