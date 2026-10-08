#pragma once

#include "MaterialGraphSceneInput.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneRuntimeEffects.h"
#include "MaterialGraphSceneSubsurface.h"
#include "MaterialGraphSceneRefraction.h"
#include "MaterialGraphSceneVolume.h"
#include "Render/Passes/Geometry/EnhancedGBufferPass.h"
#include "Render/Graph/EnhancedForwardLighting.h"
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
        // false integrates every changed lookup pixel at the reference counts
        // (gates and offline captures). true gives changed pixels the final
        // split-sum approximation; nothing refines them toward the reference.
        bool lookupApproximate = false;
        std::uint64_t subsurfaceBytes = 512ull << 20;
        std::uint64_t refractionBytes = 512ull << 20;
        std::uint64_t volumeBytes = 512ull << 20;
        // 기존 approximate cache/readback과 분리된 live 경로. split-sum 정책을
        // 명시적으로 선택한 경우에만 켜며 special 효과도 제한된 tile로 평가한다.
        bool lookupRuntimeEvaluation = false;
    };

    struct SceneProgramStats
    {
        std::uint64_t compileSubmissions{}, workerExecutions{}, failedPreparations{}, publications{},
            stalePublications{}, failedSubmissions{};
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
        bool RequestProgram(const EnhancedFrameContext& context, own::shared_owner<const Generation> generation,
                            std::string& error);
        void PollPrograms(const EnhancedFrameContext& context);
        bool IsProgramReady(const own::shared_owner<const Generation>& generation, RHIShaderBinary backend) const;
        SceneProgramStats ProgramStats() const;
        // Submitted commands, not a GPU-visible draw count.
        uint32_t ShadowDrawCount() const;
        // GPU candidates per cascade (direct compatibility uses CPU-visible
        // casters). No GPU visibility counter is read back here.
        std::array<uint32_t, 3> ShadowCasterCounts() const;
        // CPU-prepared route categories, not GPU-visible counts. Graph raster
        // bins contain one candidate each and produce a GPU 0/1 instance count.
        GpuGeometryVisibility::PreparedStats CameraVisibilityStats() const;
        std::array<GpuGeometryVisibility::PreparedStats, 3> ShadowVisibilityStats() const;
        MeshSurfaceCacheStats GeometryStats() const
        {
            return geometry_.CacheStats();
        }
        // Select only exact requested graph instances and coverage for the current
        // view. Pending requests defer the whole frame; failed requests reject it.
        bool SelectReadyInput(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> requested,
                              std::shared_ptr<const SceneViewInput>& result, std::string& error);
        // Texture copy commands must precede the native parallel prefix. Uniforms
        // and geometry are prepared afterward in its fresh upload recording.
        bool PrepareResidency(const EnhancedFrameContext& context, const std::shared_ptr<const SceneViewInput>& input,
                              std::string& error) const;
        bool Prepare(const EnhancedFrameContext& context, std::shared_ptr<const SceneViewInput> input,
                     RHITextureHandle environment, RHITextureHandle irradiance, RHITextureHandle prefiltered,
                     const EnhancedShadowData& shadow, const SceneHostBudget& budget, std::string& error,
                     std::uint64_t environmentGeneration, std::array<RHITextureHandle, 3> importance = {},
                     RHITextureHandle source = {});
        bool PreparationDeferred() const
        {
            return selectionDeferred_ || lookup_.PreparationDeferred() || runtimeEffects_.PreparationDeferred();
        }
        bool SelectionDeferred() const
        {
            return selectionDeferred_;
        }
        RGHandle DeclareShadow(EnhancedRenderGraph& graph, RGHandle shadowMap) const;
        EnhancedGBufferPass::Outputs DeclareGBuffer(EnhancedRenderGraph& graph,
                                                    const EnhancedGBufferPass::Outputs& inputs,
                                                    bool hasOccluderDepth = true) const;
        // Baseline is the existing Decal pass snapshot, ordered diffuse, ORM, normal.
        // Register after Decal.Declare and before Color; unchanged channels retain
        // the graph's full precision values instead of the quantized GBuffer.
        void DeclareDecalInputs(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                const std::array<RGHandle, 3>& baseline) const;
        RGHandle DeclareColor(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs, RGHandle lighting,
                              RGHandle ambientOcclusion, RGHandle shadowMap) const;
        struct ForwardDraw
        {
            std::size_t index{}, geometryKey{};
            float depth{};
        };
        std::vector<ForwardDraw> BlendedDraws() const;
        RGHandle DeclareBlended(EnhancedRenderGraph& graph, std::size_t index, RGHandle lighting, RGHandle shadowMap,
                                const EnhancedForwardLighting& forward) const;
        RGHandle DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
        bool HasDraws() const;
        // Live callers provide the exact graph batch ticket; pending completion is
        // polled without waiting. An empty ticket requires prior successful CPU drain.
        bool PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                                   RHISubmissionTicket ticket = {});
        RHIBufferHandle LookupStatistics() const;
        RGHandle GraphLookupStatistics(const EnhancedRenderGraph& graph) const;
        std::shared_ptr<const SceneLookupFrame> LookupFrame() const;
        std::shared_ptr<const SceneLookupFrame> ForwardLookupFrame() const;
        EnhancedGBufferPass::Outputs ForwardSurfaceOutputs() const;
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
            bool submitted{}, accepted{}, decided{};
        };
        bool PrepareProgram(const EnhancedFrameContext& context, const Instance& instance,
                            std::shared_ptr<const Program>& result, std::string& error);
        void PruneFailedPreparations(const own::shared_owner<const Generation>& requested = {});
        IRenderDeviceServices* device_{};
        MeshSurfaceEvaluator geometry_;
        GpuGeometryVisibility visibility_;
        RenderBindingCache bindings_;
        SceneLookupCache lookup_;
        SceneRuntimeEffectsResources runtimeEffects_;
        SceneSubsurfaceResources subsurface_;
        SceneRefractionResources refraction_;
        SceneVolumeResources volume_;
        std::vector<std::shared_ptr<const Program>> programs_;
        std::shared_ptr<const Frame> frame_;
        job_scheduler& scheduler_;
        IRenderDeviceServices* programDevice_{};
        std::vector<std::shared_ptr<Preparation>> preparations_;
        std::map<SlotKey, std::shared_ptr<Slot>> slots_;
        bool selectionDeferred_{};
        std::map<std::uint64_t, Recording> recordings_;
        mutable std::mutex recordingMutex_;
        std::uint64_t completed_{}, selectionSerial_{};
        SceneProgramStats stats_;
        bool CommitSubmittedFrame(const std::shared_ptr<const Frame>& frame, RHICompletionPoint completion,
                                  std::string& error);
        void PollSubmittedFrames();
        void DeclareGeometry(EnhancedRenderGraph& graph) const;
        RGHandle DeclareShading(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                bool transmissionStage, std::optional<std::size_t> blended = {},
                                EnhancedForwardLighting forward = {}) const;
        RGHandle DeclareRuntimeEffects(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                       RGHandle lighting, RGHandle ambientOcclusion, RGHandle shadowMap,
                                       bool transmissionStage, std::optional<std::size_t> blended,
                                       EnhancedForwardLighting forward) const;
        void DeclareForwardGBuffer(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                   std::size_t index) const;
        void DeclareRefractionCapture(EnhancedRenderGraph& graph, const EnhancedGBufferPass::Outputs& inputs,
                                      std::optional<std::size_t> index = {}) const;
        void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
        void OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion) override;
        void OnUploadCompleted(std::uint64_t completed) override;
        void OnUploadAborted(std::uint64_t recording) override;
        void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint reservedCompletion) override;
    };
} // namespace material_graph
