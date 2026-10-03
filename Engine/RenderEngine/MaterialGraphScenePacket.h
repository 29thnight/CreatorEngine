#pragma once

#include "MaterialGraphIblBake.h"
#include "MaterialGraphSurfaceBatch.h"
#include "MaterialGraphRenderBindings.h"
#include "Render/Graph/EnhancedRenderPass.h"

namespace material_graph
{
// The evaluating host attaches the exact immutable instance and spatial/view
// revisions. This boundary does not evaluate a graph or interpolate samples.
struct SceneSurfaceEvaluation
{
    std::shared_ptr<const Instance> instance;
    std::uint64_t sceneEpoch{};
    std::uint64_t viewRevision{};
    std::uint64_t geometryRevision{};
    std::vector<IblBakePoint> points;
    // Exactly one input transport: CPU points or an owning GPU evaluation.
    std::shared_ptr<const SurfaceBatch> gpu;
};

bool BuildSceneSurfaceEvaluation(std::shared_ptr<const SurfaceBatch> gpu, SceneSurfaceEvaluation& result,
                                 std::string& error);

enum class SceneCoverage : std::uint8_t
{
    Opaque,
    Masked,
    Blended
};

// Route answers how the material is shaded; coverage answers where it sorts
// and whether depth is written. An opaque Core material still uses Forward.
bool ClassifySceneCoverage(const EnhancedMaterialCoverage& coverage, SceneCoverage& result, std::string& error);

struct ScenePassLayout
{
    PassLayout material;
    std::uint32_t iblSlot{};
};

// Append the evaluated-point IBL root SRV, then reflected material ranges.
// Reject host overlaps before asking either backend to create a layout.
bool CreateScenePassLayout(IRenderRootSignatureCache& cache, const BindingLayout& material,
                           std::span<const RHIPipelineLayoutParam> hostParameters,
                           std::span<const RHIStaticSamplerDesc> hostSamplers, bool inputAssembler,
                           ScenePassLayout& result, std::string& error, std::uint32_t iblRegister = 0);

struct SceneMaterialPacket
{
    std::uint64_t serial{};
    Selection selection;
    SceneCoverage queue{};
    EnhancedMaterialCoverage coverage;
    SceneSurfaceEvaluation evaluation;
    IblEnvironment environment;
    std::shared_ptr<const RenderBindings> bindings;
    std::shared_ptr<const LX::Runtime::GraphicsPipeline> pipeline;
    std::shared_ptr<const IblBakeResult> ibl;
    std::uint32_t iblSlot{};
};

// One render-thread owner for one draw/view slot. Prepare returns a packet for
// the current recording; PublishSubmitted commits only after host-confirmed
// successful native submission. RHI allocation callbacks can precede submission.
// Transaction callbacks retain even failed recorded candidates until GPU
// completion, and discard a wholly aborted recording without publication.
// Shader host ABI, point evaluation, graph usages and GPU texture-cache
// residency remain the installing pass's responsibility. No Scene capability
// is enabled merely by constructing this owner.
class SceneMaterialSlot final : private IRHIUploadTransactionListener
{
  public:
    SceneMaterialSlot() = default;
    ~SceneMaterialSlot();
    SceneMaterialSlot(const SceneMaterialSlot&) = delete;
    SceneMaterialSlot& operator=(const SceneMaterialSlot&) = delete;

    bool Initialize(IRenderDeviceServices& device, std::string& error);

    // The host supplies fixed-function state with no shader pointers. Blend,
    // depth-write and cull policy must agree with the separate coverage policy.
    bool Prepare(IRenderTextureCache& textures, IRenderPipelineCache& pipelines, RenderBindingCache& bindings,
                 IblBaker& baker, const SceneSurfaceEvaluation& evaluation, const IblEnvironment& environment,
                 const EnhancedMaterialCoverage& coverage, const ScenePassLayout& layout,
                 const RHIGraphicsPipelineDesc& pipeline, RHIShaderBinary backend, const Capabilities& capabilities,
                 const Budget& budget, std::shared_ptr<const SceneMaterialPacket>& result, std::string& error);

    bool Bind(RHIEncoder& encoder, const SceneMaterialPacket& packet, std::string& error) const;
    // GPU-evaluated packets additionally need completed submission and accepted
    // point/source readback. A failed/pending decision preserves the active packet.
    bool PublishSubmitted(std::uint64_t recordingId, RHICompletionPoint completion, std::string& error);
    // A rejected publication still retains submitted GPU resources to completion.
    void RejectSubmitted(std::uint64_t recordingId);
    const std::shared_ptr<const SceneMaterialPacket>& Active() const { return active_; }
    std::size_t RetainedRecordingCount() const { return recordings_.size(); }

    // Required after device idle and before device/cache destruction. External
    // packet holders must also be released before the owning device dies.
    void ShutdownAfterIdle();

  private:
    struct Recording
    {
        std::vector<std::shared_ptr<const SceneMaterialPacket>> owners;
        std::shared_ptr<const SceneMaterialPacket> accepted;
        std::shared_ptr<const SceneMaterialPacket> submitted;
        std::uint64_t completion{};
        bool submissionNotified{};
        bool publicationDecided{};
    };

    void OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recordingId) override;

    IRenderDeviceServices* device_{};
    std::uint64_t serial_{};
    std::uint64_t completed_{};
    std::shared_ptr<const SceneMaterialPacket> active_;
    std::map<std::uint64_t, Recording> recordings_;
};
} // namespace material_graph
