#pragma once

#include "MaterialGraphSceneInput.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneSubsurface.h"
#include "MaterialGraphSceneRefraction.h"
#include "MaterialGraphSceneVolume.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "JobScheduler.h"
#include "RHI/RHISubmissionThread.h"

#include <map>
#include <mutex>
#include <tuple>

namespace material_graph
{
// Exact evaluated-pixel cache. Work and memory scale with the viewport; a cold
// cache is still a reference integrator and needs a separate real-time cost gate.
struct SceneHostBudget
{
    std::uint32_t pixels = 4096u * 4096u;
    std::uint32_t draws = 4096;
    std::uint64_t lookupBytes = 2ull << 30;
    std::uint64_t subsurfaceBytes = 512ull << 20;
    std::uint64_t refractionBytes = 512ull << 20;
    std::uint64_t volumeBytes = 512ull << 20;
};

struct SceneProgramStats
{
    std::uint64_t compileSubmissions{}, workerExecutions{}, failedPreparations{}, publications{}, stalePublications{},
        failedSubmissions{};
    std::size_t pending{}, ready{}, activeSlots{}, retainedRecordings{};
    std::string lastError;
};

class SceneHost final : private IRHIUploadTransactionListener
{
  public:
    explicit SceneHost(job_scheduler& scheduler = ce::get_job_scheduler());
    ~SceneHost();
    SceneHost(const SceneHost&) = delete;
    SceneHost& operator=(const SceneHost&) = delete;

    // Shader verification runs on the scheduler. Poll admits a bounded batch
    // of PSO requests; D3D12/Vulkan create them asynchronously.
    bool RequestProgram(const EnhancedFrameContext& context, std::shared_ptr<const Generation> generation,
                        std::string& error);
    void PollPrograms(const EnhancedFrameContext& context);
    bool IsProgramReady(const std::shared_ptr<const Generation>& generation, RHIShaderBinary backend) const;
    SceneProgramStats ProgramStats() const;
    MeshSurfaceCacheStats GeometryStats() const { return geometry_.CacheStats(); }
    // Current geometry/camera stay current. Pending/failed replacements use the
    // submitted instance and coverage for the same epoch/view/Material slot.
    // A cold slot has no accepted material and is omitted until preparation ends.
    bool SelectReadyInput(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> requested,
                          std::shared_ptr<const SceneViewInput>& result, std::string& error);
    // Texture copy commands must precede the native parallel prefix. Uniforms
    // and geometry are prepared afterward in its fresh upload recording.
    bool PrepareResidency(const EnhancedFrameContext& context, const std::shared_ptr<const SceneViewInput>& input,
                          std::string& error) const;
    bool Prepare(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> input,
                 RHITextureHandle environment, RHITextureHandle irradiance, RHITextureHandle prefiltered,
                 const EnhancedShadowData& shadow, const SceneHostBudget& budget,
                 std::string& error, std::uint64_t environmentGeneration);
    void DeclareShadow(EnhancedRenderGraph& graph, RGHandle shadowMap) const;
    void DeclareGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const;
    // Baseline is the existing Decal pass snapshot, ordered diffuse, ORM, normal.
    // Register after Decal.Declare and before Color; unchanged channels retain
    // the graph's full precision values instead of the quantized GBuffer.
    void DeclareDecalInputs(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                            const std::array<RGHandle, 3>& baseline) const;
    void DeclareColor(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs, RGHandle lighting,
                      RGHandle ambientOcclusion, RGHandle shadowMap) const;
    RGHandle DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
    bool HasDraws() const;
    // Live callers provide the exact graph batch ticket; pending completion is
    // polled without waiting. An empty ticket requires prior successful CPU drain.
    bool PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                               RHISubmissionTicket ticket = {});
    RHIBufferHandle LookupStatistics() const;
    RGHandle GraphLookupStatistics(const EnhancedRenderGraph& graph) const;
    std::shared_ptr<const SceneLookupFrame> LookupFrame() const;
    std::shared_ptr<const SceneSubsurfaceFrame> SubsurfaceFrame() const;
    std::shared_ptr<const SceneRefractionFrame> RefractionFrame() const;
    std::shared_ptr<const SceneVolumeFrame> VolumeFrame() const;
    // Release after graph/submission owners have drained and before the device.
    void ShutdownAfterIdle();

  private:
    struct Program;
    struct Frame;
    struct Preparation;
    struct Slot;
    using SlotKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    struct Recording
    {
        std::vector<std::shared_ptr<const Frame>> owners;
        std::shared_ptr<const Frame> publication;
        RHISubmissionTicket ticket;
        std::uint64_t completion{};
        bool submitted{}, decided{};
    };
    bool PrepareProgram(const EnhancedFrameContext& context, const Instance& instance,
                        std::shared_ptr<const Program>& result, std::string& error);
    IRenderDeviceServices* device_{};
    MeshSurfaceEvaluator geometry_;
    RenderBindingCache bindings_;
    SceneLookupCache lookup_;
    SceneSubsurfaceResources subsurface_;
    SceneRefractionResources refraction_;
    SceneVolumeResources volume_;
    std::vector<std::shared_ptr<const Program>> programs_;
    std::shared_ptr<const Frame> frame_;
    job_scheduler& scheduler_;
    IRenderDeviceServices* programDevice_{};
    std::vector<std::shared_ptr<Preparation>> preparations_;
    std::map<SlotKey, std::shared_ptr<Slot>> slots_;
    std::map<std::uint64_t, Recording> recordings_;
    mutable std::mutex recordingMutex_;
    std::uint64_t completed_{}, selectionSerial_{};
    SceneProgramStats stats_;
    bool CommitSubmittedFrame(const std::shared_ptr<const Frame>& frame, RHICompletionPoint completion,
                              std::string& error);
    void PollSubmittedFrames();
    void DeclareGeometry(EnhancedRenderGraph& graph) const;
    void DeclareShading(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs, RGHandle lighting,
                        RGHandle ambientOcclusion, RGHandle shadowMap, bool transmissionStage) const;
    void DeclareTransmissionGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const;
    void DeclareRefractionCapture(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs) const;
    void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recording) override;
};
} // namespace material_graph
