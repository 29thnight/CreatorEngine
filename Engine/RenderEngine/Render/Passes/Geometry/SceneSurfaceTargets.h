#pragma once
#include "../../Graph/EnhancedRenderPass.h"
#include "../../../../Utility_Framework/StandardLibrary.h"

// Resource contract for METEOR surfaces. Owns allocation/clear only; no geometry,
// material pipelines, draw batches, culling or alternate surface route.
class SceneSurfaceTargets final
{
public:
    static constexpr uint32_t kRenderTargetCount = 6;
    static constexpr RHIFormat kDepthFormat = RHIFormat::D32Float;
    struct Outputs {
        RGHandle diffuse, metalRough, normal, emissive, bitmask, depth, visibility;
        // Optional immutable pre-Decal versions, ordered diffuse/ORM/normal.
        // Carried with the post-Decal surface; never registered in SceneHost state.
        std::array<RGHandle,3> decalBaseline{};
    };
    static RHIFormat GetRenderTargetFormat(uint32_t index);
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    void DeclareVisibility(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    void DeclareSurface(EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
        RGHandle depth, RGHandle visibility);
    const Outputs& GetOutputs() const { return m_outputs; }
    void Shutdown() { m_outputs = {}; }
private:
    Outputs m_outputs{};
};
