#pragma once
#include "../../Core/PassResourceRetirement.h"
#include "../../Graph/EnhancedForwardLighting.h"
#include "../../Graph/EnhancedRenderPass.h"
#include "../../../LXMaterialPipeline.h"

#include <cstdint>
#include <string>

// Screen tiles x light spheres. Each 16x16 tile keeps at most 32 light indices
// bounded by the opaque depth range; METEOR blended shading reads the lists.
// The pass owns only culling. Surface shading belongs to the METEOR SceneHost.
class EnhancedLightTilePass : public EnhancedRenderPass
{
public:
    static constexpr uint32_t kTileSize = EnhancedForwardLighting::TileSize;
    static constexpr uint32_t kMaxLightsPerTile = EnhancedForwardLighting::MaxLightsPerTile;

    const char* GetName() const override { return "METEOR.Scene.LightTiles"; }
    bool Initialize(const EnhancedFrameContext& context, std::string& outError) override;
    bool PrepareFrame(const EnhancedFrameContext& context, std::string& outError) override;
    // Declares nothing unless the frame has a light-tile consumer.
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context) override;
    void Shutdown() override;

    void SetDepth(RGHandle depth) { m_depth = depth; }
    // Oracle only: blended shading loops every light instead of its tile list.
    void SetUseReferencePath(bool use) { m_reference = use; }
    // Valid only after a Declare that culled: buffers, graph handles and lights.
    const EnhancedForwardLighting& GetLighting() const { return m_lighting; }

    RHIBufferHandle GetTileCountBuffer() const { return m_tileCountBuffer; }
    RGHandle GetTileCountHandle() const { return m_lighting.graphCounts; }

private:
    bool EnsureTileBuffers(const EnhancedFrameContext& context, std::string& outError);

    PassResourceRetirement m_resourceRetirement;
    LX::Runtime::ComputePipeline m_cullPSO;
    RGHandle m_depth;
    bool m_reference{false};
    EnhancedForwardLighting m_lighting{};
    uint32_t m_tileCountX{0};
    uint32_t m_tileCountY{0};
    uint32_t m_allocatedTiles{0};
    RHIBufferHandle m_tileCountBuffer;
    RHIBufferHandle m_tileListBuffer;
    RHIResourceState m_tileCountState{RHIResourceState::Common};
    RHIResourceState m_tileListState{RHIResourceState::Common};
};
