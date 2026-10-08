#pragma once

#include "FrameCameraSnapshot.h"
#include "MaterialGraphInstancePins.h"
#include "Assets/ModelAssetGeneration.h"
#include "MaterialGraphMeshSurface.h"
#include "MaterialGraphSceneCoverage.h"

class Material;

namespace material_graph
{
    struct SceneMaterialSource
    {
        own::shared_owner<const Instance> instance;
        EnhancedMaterialCoverage coverage;
        std::uint64_t materialSlot{};

        // Producer-side capture. RenderThread reads this immutable source, not the
        // mutable Material object. Null means the producer selected a legacy source.
        static std::shared_ptr<const SceneMaterialSource> Capture(const Material& material);
        static std::shared_ptr<const SceneMaterialSource> Capture(own::shared_owner<const Instance> instance,
                                                                  SceneCoverage queue, bool doubleSided,
                                                                  std::uint64_t materialSlot = 0);
    };

    struct SceneInputView
    {
        std::uint64_t frameId{}, sceneEpoch{}, viewId{}, historyRevision{};
        std::uint32_t width{}, height{};
        FrameCameraSnapshot camera;
    };

    struct SceneInputBudget
    {
        MeshSurfacePlanBudget mesh{.maxChunkPoints = 1u << 20};
        std::uint32_t draws = 4096;
        std::uint32_t chunks = 4096;
        std::uint64_t cpuPayloadBytes = 256ull << 20;
        std::uint64_t gpuPayloadBytes = 512ull << 20;
    };

    struct SceneInputCost
    {
        std::uint64_t sourceVertices{}, triangles{}, chunks{}, cpuPayloadBytes{}, gpuPayloadBytes{};
    };

    struct SceneDrawInput
    {
        std::size_t sourceIndex{};
        std::size_t geometryKey{};
        assets::ModelAssetGenerationHandle model;
        assets::ModelMeshHandle mesh;
        std::size_t modelPinIndex = SIZE_MAX;
        std::uint64_t materialSlot{}, selectionRevision{};
        own::local_view<const Instance> material;
        std::size_t materialPinIndex{ InstanceFramePins::InvalidIndex };
        EnhancedMaterialCoverage coverage;
        SceneCoverage queue{};
        float viewDepth{};
        math::vector3 shadowCenter{};
        float shadowRadius{};
        std::shared_ptr<const MeshSurfacePlan> geometry;
    };

    // One selected Scene view. No proxy, Material, Camera, mutable mesh bytes or
    // palette pointers survive sealing. One frame table retains each exact graph
    // instance; draw records contain only its table index and anchored borrow.
    // A fresh revision identifies the entire view's current geometry, not just a
    // model generation; changing world/pose/camera cannot reuse another view's batch.
    class SceneViewInput
    {
      public:
        static bool Seal(const SceneInputView& view, std::span<const EnhancedDrawItem> draws,
                         const SceneInputBudget& budget, std::shared_ptr<const SceneViewInput>& result, std::string& error,
                         own::shared_owner<const assets::ModelAssetGenerationPins> modelPins = {},
                         own::shared_owner<InstanceFramePins> materialPins = {},
                         const assets::ModelGeometryPreparationPins* geometryPins = nullptr);
        const SceneInputView& View() const { return view_; }
        const SurfaceView& Surface() const { return surface_; }
        const math::matrix4x4& ViewProjection() const { return viewProjection_; }
        std::span<const SceneDrawInput> Draws() const { return draws_; }
        const SceneInputCost& Cost() const { return cost_; }
        own::shared_owner<const Instance> MaterialOwner(const SceneDrawInput& draw) const
        {
            return materialPins_->Owner(draw.materialPinIndex);
        }
        const own::shared_owner<InstanceFramePins>& MaterialPins() const { return materialPins_; }

      private:
        friend class SceneHost;
        SceneViewInput() = default;
        SceneInputView view_;
        SurfaceView surface_;
        math::matrix4x4 viewProjection_;
        std::vector<SceneDrawInput> draws_;
        SceneInputCost cost_;
        own::shared_owner<const assets::ModelAssetGenerationPins> modelPins_;
        own::shared_owner<InstanceFramePins> materialPins_;
    };
} // namespace material_graph
