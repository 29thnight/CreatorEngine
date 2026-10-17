#pragma once

#include "MaterialGraphSceneHost.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "Render/Graph/EnhancedRenderPass.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"

#include "../../../../../../Engine/Utility_Framework/StandardLibrary.h"

class DX12DeviceResources;
struct Vertex;

// Product shadow casters for isolated DX12 tests. The live renderer draws every
// caster through the material graph Scene host; this fixture follows that same
// order (PrepareGraphFrame -> Scene seal/prepare -> DeclareGraphTargets ->
// DeclareShadow) with one default material graph shared by all synthetic meshes.
//
// A Scene host frame is only complete, publishable and retired once its camera
// route (GBuffer -> Color, which records the lighting lookup) is declared too.
// The fixture declares that route into its own private targets with a sink, so
// the test's own camera passes and images are unaffected.
class GraphSceneFixture
{
public:
    GraphSceneFixture() = default;
    GraphSceneFixture(const GraphSceneFixture&) = delete;
    GraphSceneFixture& operator=(const GraphSceneFixture&) = delete;
    ~GraphSceneFixture() { Shutdown(); }

    // Compiles the default graph and waits for its Scene programs.
    bool Initialize(const EnhancedFrameContext& context, std::string& error, float alpha = 1.f);

    // Copies a static triangle list into the core model vertex layout.
    // Returns the mesh index used by Draw.
    std::uint32_t AddMesh(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices);

    void BuildPins(assets::ModelAssetGenerationPins& descriptors, assets::ModelGeometryPreparationPins& geometry) const;

    // A graph caster draw. Mesh storage outlives every draw made from it.
    EnhancedDrawItem Draw(std::uint32_t mesh, const math::matrix4x4& world) const;

    // Converts a scene draw into a graph caster with the same geometry and pose.
    // Geometry bytes are copied once per geometry key into fixture-owned storage,
    // so pinned/granular product provenance is not required by the seal.
    EnhancedDrawItem Caster(const EnhancedDrawItem& source);

    // Call inside an open frame after context.shadowDraws/camera/lights are set.
    // Runs shadow.PrepareGraphFrame, then seals and prepares the Scene casters.
    bool Prepare(EnhancedFrameContext& context, EnhancedShadowPass& shadow, std::string& error);

    // Declares the cleared cascades, the material graph shadow casters and the
    // host's private camera route. Returns the shadow map version that
    // receivers must read.
    RGHandle Declare(EnhancedRenderGraph& graph, EnhancedShadowPass& shadow,
        const EnhancedFrameContext& context);

    // After EndFrame: publishes the submitted Scene frame and retires it on the
    // GPU before the next Prepare. Waits for the GPU even on failure.
    // A parallel recording passes its submission ticket; its recorded batch owns
    // the completion point and must carry the same frame ID.
    bool Publish(DX12DeviceResources& resources, std::uint64_t frameId, std::string& error,
        const RHISubmissionTicket& ticket = {});

    std::uint32_t ShadowDrawCount() const { return m_host.ShadowDrawCount(); }
    auto ShadowCacheStats() const { return m_host.CurrentShadowCacheStats(); }
    std::array<std::uint32_t, 3> RecordedShadowMeshletDrawCounts() const
        { return m_host.RecordedShadowMeshletDrawCounts(); }
    auto ShadowMeshletOutputs() const { return m_host.ShadowMeshletOutputs(); }
    void Shutdown();

    bool PrepareFrame(EnhancedFrameContext& context, std::string& error,
        const EnhancedShadowData& shadow = {});
    void DeclareSurface(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    RGHandle DeclareColor(EnhancedRenderGraph& graph, const EnhancedFrameContext& context,
        RGHandle lighting, RGHandle shadowMap);
    void Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    std::size_t BlendedDrawCount() const { return m_host.BlendedDraws().size(); }
    std::size_t PreparedDrawCount() const { return m_host.HasDraws() ? m_host.PreparedMeshletDrawCount() : 0; }
    // METEOR.Scene.Blended over the composed opaque color, lit through light tiles.
    RGHandle DeclareBlended(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle shadowMap,
        const EnhancedForwardLighting& tiles) const
    {
        return m_host.DeclareBlendedStream(graph, m_layerWork, lighting, shadowMap, tiles);
    }
    const SceneSurfaceTargets::Outputs& GetOutputs() const { return m_surfaceOutputs; }
    uint32_t GetLastDrawCount() const { return static_cast<uint32_t>(m_surfaceDraws.size()); }
    uint32_t GetLastBatchCount() const { return m_host.PreparedMeshletDrawCount(); }
    uint32_t GetLastSkinnedCount() const;
    uint32_t GetLastBonePaletteCount() const;
    static bool CompleteSubmission(DX12DeviceResources& resources, std::string& error);
private:
    void CookMesh(uint32_t index);
    struct MeshStorage
    {
        std::vector<std::uint8_t> vertices;
        std::vector<std::uint32_t> indices;
        RHIModelMeshView view{};
        own::shared_owner<assets::ModelMeshDescriptor> descriptor;
        own::shared_owner<const assets::ModelGeometryPayload> payload;
    };

    material_graph::SceneHost m_host;
    // Private camera route. Its context outlives graph execution because pass
    // callbacks read it; it renders no legacy draws.
    SceneSurfaceTargets m_cameraTargets;
    EnhancedFrameContext m_cameraContext{};
    std::vector<EnhancedDrawItem> m_surfaceDraws;
    SceneSurfaceTargets::Outputs m_surfaceOutputs{};
    material_graph::SceneLayerWork m_layerWork{};
    material_graph::SceneColorInputs m_colorInputs{};
    own::shared_owner<const material_graph::Generation> m_generation;
    own::shared_owner<const material_graph::Instance> m_instance;
    std::uint32_t AddView(const RHIModelMeshView& source);

    std::deque<MeshStorage> m_meshes;
    std::unordered_map<std::size_t, std::uint32_t> m_sourceMeshes;
    std::uint64_t m_sceneEpoch{1};
    bool m_initialized{};
};
