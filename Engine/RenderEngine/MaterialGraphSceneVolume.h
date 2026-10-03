#pragma once

#include "MaterialGraphSceneInput.h"
#include "MaterialGraphRenderBindings.h"
#include "LXMaterialPipeline.h"

#include <array>

namespace material_graph
{
// Closed homogeneous media. The triangle bound also bounds every ray's event
// list; the integrator never truncates geometry or intersection intervals.
inline constexpr std::uint32_t SceneVolumeMaxObjects = 16;
inline constexpr std::uint32_t SceneVolumeMaxTriangles = 128;
inline constexpr std::uint32_t SceneVolumeConstantsBytes = 4464;

struct SceneVolumeTriangle
{
    math::vector4 a, b, c;
};
static_assert(sizeof(SceneVolumeTriangle) == 48);

struct SceneVolumeCoefficient
{
    math::vector4 scatteringAnisotropy, absorption, emission;
};
static_assert(sizeof(SceneVolumeCoefficient) == 48);

struct SceneVolumeBinding
{
    std::shared_ptr<const RenderBindings> material;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline;
    RHIBufferSlice constants;
};

class SceneVolumeFrame
{
  public:
    ~SceneVolumeFrame();
    void DeclareCoefficients(EnhancedRenderGraph& graph, std::span<const SceneVolumeBinding> bindings) const;
    RGHandle DeclareComposite(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth, RGHandle shadow) const;
    RHIBufferSlice Triangles() const { return triangles_; }
    RHIBufferHandle Coefficients() const { return coefficients_; }
    RHIBufferSlice TransportConstants() const { return constants_.front(); }
    std::array<RHIBindingDesc, 2> LightingBindings(RHITextureHandle shadow) const;
    RGHandle GraphCoefficients(const EnhancedRenderGraph& graph) const;
    std::uint32_t TriangleCount() const { return triangleCount_; }
    std::uint32_t ObjectCount() const { return objectCount_; }

  private:
    friend class SceneVolumeResources;
    IRenderDeviceServices* device_{};
    std::uint32_t width_{}, height_{}, triangleCount_{}, objectCount_{};
    std::uint64_t recording_{}, descriptors_{};
    RHIBufferSlice triangles_;
    RHIBufferHandle coefficients_;
    RHITextureHandle environment_;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> composite_;
    RHIBindingTable coefficientOutput_;
    std::vector<RHIBufferSlice> constants_;
    std::weak_ptr<const SceneVolumeFrame> self_;
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable RGHandle graphCoefficients_, output_;
    void CheckCurrent(const EnhancedRenderGraph& graph) const;
};

class SceneVolumeResources
{
  public:
    bool Prepare(const EnhancedFrameContext& context, const SceneViewInput& input, RHITextureHandle environment,
                 const EnhancedShadowData& shadow, std::uint64_t memoryBudget,
                 std::shared_ptr<const SceneVolumeFrame>& result, std::string& error);
    void ShutdownAfterIdle();

  private:
    IRenderDeviceServices* device_{};
    LX::Runtime::ComputePipeline composite_;
    bool Initialize(const EnhancedFrameContext& context, std::string& error);
};
} // namespace material_graph
