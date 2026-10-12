#pragma once
#include "../../Graph/EnhancedRenderPass.h"
#include <array>

// Resource contract for METEOR surfaces. Owns allocation/clear only; no geometry,
// material pipelines, draw batches, culling or alternate surface route.
class SceneSurfaceTargets final
{
public:
    static constexpr uint32_t kRenderTargetCount = 5;
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;
    struct Outputs { RGHandle diffuse, metalRough, normal, emissive, bitmask, depth; };
    static RHIFormat GetRenderTargetFormat(uint32_t index);
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    const Outputs& GetOutputs() const { return m_outputs; }
    void Shutdown() { m_outputs = {}; }
private:
    Outputs m_outputs{};
};
