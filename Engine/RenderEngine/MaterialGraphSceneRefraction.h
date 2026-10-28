#include "../Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "LXMaterialPipeline.h"

#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneVolume.h"

namespace material_graph
{
struct SceneRefractionSample
{
    math::vector4 glassSingleAlbedo, glassAverage, glassEnvironment, rawTransmission, radiance;
};
static_assert(sizeof(SceneRefractionSample) == 80);

class SceneRefractionFrame
{
  public:
    ~SceneRefractionFrame();
    std::span<const RHITextureHandle> Inputs() const { return inputs_; }
    RHIBufferHandle Samples() const { return samples_; }
    const ce::fixed_array<RGHandle, 2>& DeclareInputs(EnhancedRenderGraph& graph) const;
    const ce::fixed_array<RGHandle, 2>& GraphInputs(const EnhancedRenderGraph& graph) const
    {
        CheckCurrent(graph);
        return graphInputs_;
    }
    ce::fixed_array<RGHandle, 2> DeclareCaptureOutputs(EnhancedRenderGraph& graph) const;
    void DeclareBackground(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth) const;
    void DeclareBake(EnhancedRenderGraph& graph, const SceneLookupFrame& lookup, RGHandle owners,
                     RGHandle shadow) const;
    RGHandle GraphSamples(const EnhancedRenderGraph& graph) const;
    RGHandle GraphBackgroundColor(const EnhancedRenderGraph& graph) const;
    RGHandle GraphBackgroundDepth(const EnhancedRenderGraph& graph) const;

  private:
    friend class SceneRefractionResources;
    IRenderDeviceServices* device_{};
    std::uint32_t width_{}, height_{};
    std::uint64_t recording_{}, descriptors_{};
    RHITextureHandle environment_;
    ce::fixed_array<RHITextureHandle, 2> inputs_{};
    RHIBufferHandle samples_;
    own::shared_owner<const LX::Runtime::ComputeGeneration> bake_;
    ce::dynamic_array<RHIBufferSlice> constants_;
    RHIBindingTable outputs_;
    own::shared_owner<const SceneVolumeFrame> volume_;
    RHIBufferSlice emptyVolumeConstants_, emptyVolumeBuffer_;
    own::weak_owner<const SceneRefractionFrame> self_;
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable ce::fixed_array<RGHandle, 2> graphInputs_{};
    mutable RGHandle graphSamples_, backgroundColor_, backgroundDepth_;
    void CheckCurrent(const EnhancedRenderGraph& graph) const;
};

class SceneRefractionResources
{
  public:
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                 RHITextureHandle environment, std::uint64_t memoryBudget,
                 own::shared_owner<const SceneRefractionFrame>& result, std::string& error,
                 own::shared_owner<const SceneVolumeFrame> volume = {});
    void ShutdownAfterIdle();

  private:
    IRenderDeviceServices* device_{};
    LX::Runtime::ComputePipeline bake_, volumeBake_;
    bool Initialize(const EnhancedFrameContext& context, bool volume, std::string& error);
};
} // namespace material_graph
