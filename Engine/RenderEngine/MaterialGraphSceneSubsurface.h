#pragma once

#include "LXMaterialPipeline.h"

#include "MaterialGraphSceneLookup.h"

namespace material_graph
{
// Separate metal/dielectric reflection for the MAT-5 Special surface composition.
struct SceneSubsurfaceReflection
{
    math::vector4 metalSingleAlbedo, metalAverage;
    math::vector4 dielectricSingleAlbedo, dielectricAverage;
    math::vector4 metalEnvironment, dielectricEnvironment;
};
static_assert(sizeof(SceneSubsurfaceReflection) == 96);

class SceneSubsurfaceFrame
{
  public:
    ~SceneSubsurfaceFrame();
    std::span<const RHITextureHandle> Inputs() const;
    RHIBufferHandle Reflection() const;
    RHIBufferHandle Irradiance() const;
    const std::array<RGHandle, 7>& DeclareInputs(EnhancedRenderGraph& graph) const;
    void DeclareReflection(EnhancedRenderGraph& graph, const SceneLookupFrame& lookup, RGHandle owners) const;
    void DeclareFilter(EnhancedRenderGraph& graph, RGHandle owners) const;
    RGHandle GraphReflection(const EnhancedRenderGraph& graph) const;
    RGHandle GraphIrradiance(const EnhancedRenderGraph& graph) const;

  private:
    friend class SceneSubsurfaceResources;
    IRenderDeviceServices* device_{};
    std::uint32_t width_{}, height_{};
    std::uint64_t recording_{}, descriptors_{};
    RHITextureHandle environment_;
    std::array<RHITextureHandle, 7> inputs_{};
    RHIBufferHandle reflection_, irradiance_;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> bake_, filter_;
    std::vector<RHIBufferSlice> constants_;
    RHIBindingTable outputs_;
    std::weak_ptr<const SceneSubsurfaceFrame> self_;
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable std::array<RGHandle, 7> graphInputs_{};
    mutable RGHandle graphReflection_, graphIrradiance_;
    void CheckCurrent(const EnhancedRenderGraph& graph) const;
};

class SceneSubsurfaceResources
{
  public:
    bool Prepare(const EnhancedFrameContext& context, RHITextureHandle environment, std::uint64_t memoryBudget,
                 std::shared_ptr<const SceneSubsurfaceFrame>& result, std::string& error);
    void ShutdownAfterIdle();

  private:
    IRenderDeviceServices* device_{};
    LX::Runtime::ComputePipeline bake_, filter_;
    bool Initialize(const EnhancedFrameContext& context, std::string& error);
};
} // namespace material_graph
