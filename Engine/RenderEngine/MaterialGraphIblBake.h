#pragma once

#include "RHI/IRenderDeviceServices.h"
#include "RHI/IRenderPipelineCache.h"
#include "RHI/RHIEncoder.h"
#include "RHI/RHIShaderBlob.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <array>
#include <atomic>
#include <memory>

namespace material_graph
{
using IblVector = std::array<float, 4>;

// Evaluated, spatially fixed sample. viewTier.w describes the reflection model:
// 0 = legacy Core, 1 = compensated Principled GGX (including core graphs).
// It is independent of the authoring tier and is not a material-wide 2D LUT.
// Fields must be finite, magnitude <= 1e6, with a nonzero front-facing view
// (NdotV >= 1e-4). Other zero directions use EvaluateMaterial's fallbacks.
struct IblBakePoint
{
    IblVector baseAlpha{.8f, .8f, .8f, 1};
    IblVector normalRoughness{0, 0, 1, .5f};
    IblVector metalIorLevelAo{0, 1.5f, .5f, 1};
    IblVector tintAnisotropy{1, 1, 1, 0};
    IblVector emissionColorStrength{1, 1, 1, 0};
    IblVector coatWeightRoughIorFilmThickness{0, .03f, 1.5f, 0};
    IblVector coatTintFilmIor{1, 1, 1, 1.33f};
    IblVector coatNormalSheenWeight{0, 0, 1, 0};
    IblVector sheenTintRoughness{1, 1, 1, .5f};
    IblVector tangentRotation{1, 0, 0, 0};
    IblVector viewTier{0, 0, 1, 0};
};
static_assert(sizeof(IblBakePoint) == 176);
bool IsValidIblBakePoint(const IblBakePoint& point);
class SurfaceBatch;

// Matches Includes/PrincipledIblLookup.slang. For model 1, singleScatter includes
// GGX compensation; it must not receive another Lambertian compensation term.
// Reflection is normalized by the
// RGB Fresnel-weighted sum; coat and sheen never reuse the base convolution.
struct IblBakeSample
{
    IblVector baseSingleAlbedo, baseAverage;
    IblVector coatSingleAlbedo, coatAverage;
    IblVector irradiance, basePrefiltered, coatPrefiltered, sheenIrradiance, coatIrradiance;
};
static_assert(sizeof(IblBakeSample) == 144);

struct IblEnvironment
{
    RHITextureEntry cube;
    std::uint64_t generation{};
    std::shared_ptr<const void> owner;
};

class IblBakeResult
{
  public:
    ~IblBakeResult();
    IblBakeResult(const IblBakeResult&) = delete;
    IblBakeResult& operator=(const IblBakeResult&) = delete;

    RHIBufferHandle Buffer() const { return buffer_; }
    std::uint32_t Count() const { return count_; }
    bool IsReady() const;
    RGHandle GraphOutput(const EnhancedRenderGraph& graph) const;
    bool Declare(EnhancedRenderGraph& graph, std::string& error) const;
    bool Matches(const IRenderDeviceServices& device, const IblEnvironment& environment,
                 std::span<const IblBakePoint> points) const;
    bool MatchesGpu(const IRenderDeviceServices& device, const IblEnvironment& environment,
                    const SurfaceBatch& points) const;

  private:
    friend class IblBaker;
    IblBakeResult() = default;
    bool IsCurrent() const;
    bool RecordCommands(RHIEncoder& encoder, std::string& error) const;
    IRenderDeviceServices* device_{};
    RHIBufferHandle buffer_;
    IblEnvironment environment_;
    std::vector<IblBakePoint> points_;
    std::shared_ptr<const SurfaceBatch> gpuPoints_;
    std::uint32_t count_{};
    RHIPipelineHandle pipeline_;
    RHIBufferSlice inputs_, uniform_;
    RHIBindingTable sourceTable_, targetTable_;
    std::uint64_t recordingId_{}, descriptorVersion_{};
    std::weak_ptr<const IblBakeResult> self_;
    mutable std::atomic<unsigned> recordedStages_{};
    mutable std::atomic<bool> declared_{};
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable RGHandle graphOutput_;
};

class IblBaker
{
  public:
    // Accept compiled/cooked CS bytecode. This class does not install a runtime
    // Slang compiler. Failed initialization preserves the accepted pipeline.
    bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots, IRenderPipelineCache& pipelines,
                    const RHIShaderBlob& shader, std::string& error);

    // Graph-external bake on the active recording's encoder. The environment
    // must already be ShaderResource. Output leaves this call ShaderResource.
    // Keep result + environment owner until submission completion, and destroy
    // them before device shutdown. A recorded result is not a completed result.
    // Retain previous results through their last submission before replacement.
    // The host also retains/touches the GPU environment owner/cache; a CPU
    // Texture owner alone does not prevent its cached GPU entry being evicted.
    // Texture/parameter/view changes require a new evaluated request; Matches
    // checks every field and environment generation, without hash collisions.
    bool Record(IRenderDeviceServices& device, const IblEnvironment& environment, std::span<const IblBakePoint> points,
                std::shared_ptr<const IblBakeResult>& result, std::string& error);

    // GPU graph evaluation -> bake, without CPU readback/stall between them.
    // The immutable batch is retained by the result. Invalid GPU evaluations
    // produce an explicit marker (baseAverage.w = -1) and zero radiance. Validate
    // completed point readback before Scene publication; this is recording only.
    bool RecordGpu(IRenderDeviceServices& device, const IblEnvironment& environment,
                   std::shared_ptr<const SurfaceBatch> points, std::shared_ptr<const IblBakeResult>& result,
                   std::string& error);
    bool PrepareGpu(IRenderDeviceServices& device, const IblEnvironment& environment,
                    std::shared_ptr<const SurfaceBatch> points, std::shared_ptr<const IblBakeResult>& result,
                    std::string& error);

    static constexpr std::uint32_t MaxPoints = 4096;

  private:
    IRenderDeviceServices* device_{};
    RHIPipelineHandle pipeline_;
    bool RecordInputs(IRenderDeviceServices& device, const IblEnvironment& environment,
                      std::span<const IblBakePoint> cpuPoints, std::shared_ptr<const SurfaceBatch> gpuPoints,
                      std::shared_ptr<const IblBakeResult>& result, std::string& error);
    bool PrepareInputs(IRenderDeviceServices& device, const IblEnvironment& environment,
                       std::span<const IblBakePoint> cpuPoints, std::shared_ptr<const SurfaceBatch> gpuPoints,
                       std::shared_ptr<const IblBakeResult>& result, std::string& error);
};
} // namespace material_graph
