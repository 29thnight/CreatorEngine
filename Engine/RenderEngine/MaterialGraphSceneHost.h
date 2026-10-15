#pragma once

// METEOR — Material Expression & Topology Editor for Optimized Rendering.
// The scene renderer that draws LX materials: geometry transform, culling,
// shadow/surface raster, colour, blended stream and media. Pass and resource
// names use the METEOR prefix. LX (Lattice eXperiment) is the node language
// itself (LX::Runtime, LX materials) and keeps its own name.

#include "MaterialGraphSceneInput.h"
#include "MaterialGraphFrameGeometry.h"
#include "MaterialGraphWork.h"
#include "MaterialGraphHybridVisibility.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneRuntimeEffects.h"
#include "MaterialGraphSceneSubsurface.h"
#include "MaterialGraphSceneRefraction.h"
#include "MaterialGraphSceneVolume.h"
#include "GpuGeometryVisibility.h"
#include "GpuGeometryOcclusion.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"
#include "Render/Graph/EnhancedForwardLighting.h"
#include "JobScheduler.h"
#include "RHI/RHISubmissionThread.h"

#include <map>
#include <mutex>
#include <tuple>
#include <vector>

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

    struct SceneGeometryRouteAudit
    {
        struct Draw
        {
            std::uint64_t geometryKey{}, temporalObjectId{}, temporalInstanceId{};
            std::uint32_t sourcePipeline{}, recordedPipeline{}, meshletCount{};
            bool meshShader{}, indirect{};
        };
        std::uint64_t sourceFrameId{}, sceneEpoch{}, viewId{};
        std::vector<Draw> draws;
    };

    // Graph values published by culling. No stage state or ownership is copied.
    struct SceneFirstCullOutputs { RGHandle list, depthCommands; };
    struct SceneCommandInputs
    {
        RGHandle surface, layer;
        std::vector<RGHandle> blended;
        std::array<RGHandle,2> material; // Surface publishes tile work + indirect arguments.
    };
    struct SceneFinalCullOutputs { RGHandle list; SceneCommandInputs commands; };

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
        uint32_t TemporalBucketSubmissionCount() const;
        uint32_t BlendedBucketSubmissionCount() const;
        std::array<uint32_t, 2> LayerBucketCounts() const;
        // Actual ExecuteIndirect calls, recorded after successful bucket submission.
        std::array<uint32_t, 3> ShadowBucketSubmissionCounts() const;
        // Logical mesh commands per cascade, before GPU culling.
        // A bucket can contain multiple commands in one native submission.
        std::array<uint32_t, 3> RecordedShadowMeshletDrawCounts() const;
        struct ShadowMeshletOutput {
            unsigned cascade{}, bin{};
            std::shared_ptr<const GpuGeometryVisibility::Frame> frame;
        };
        // Inspection retains the exact submitted buffers through GPU readback.
        std::vector<ShadowMeshletOutput> ShadowMeshletOutputs() const;

        // Prepared lattice mesh draw bins, before GPU visibility rejection.
        uint32_t PreparedMeshletDrawCount() const;
        // Read only after graph recording joins. Entries are appended by the
        // one METEOR.Scene.GBuffer callback after its exact draw/dispatch succeeds;
        // shadow, occluder and temporal replay draws are deliberately excluded.
        void EnableGeometryRouteAudit(bool enabled) const;
        SceneGeometryRouteAudit RecordedGeometryRoutes() const;
        // GPU candidates per cascade (direct compatibility uses CPU-visible
        // casters). No GPU visibility counter is read back here.
        std::array<uint32_t, 3> ShadowCasterCounts() const;
        // CPU-prepared camera meshlet work (opaque union + ordered blended list),
        // not GPU-visible counts.
        GpuGeometryVisibility::PreparedStats CameraVisibilityStats() const;
        // Diagnostic owner for submitted GPU work-list/argument readback gates.
        std::shared_ptr<const GpuGeometryVisibility::Frame> CameraMeshletWork() const;
        std::shared_ptr<const GpuGeometryVisibility::Frame> CameraDepthMeshletWork() const;
        std::array<GpuGeometryVisibility::PreparedStats, 3> ShadowVisibilityStats() const;
        MeshSurfaceCacheStats GeometryStats() const
        {
            return geometry_.CacheStats();
        }
        // Select only exact requested graph instances and coverage for the current
        // view. Pending requests defer the whole frame; failed requests reject it.
        bool SelectReadyInput(const EnhancedFrameContext& context, own::shared_owner<const SceneViewInput> requested,
                              own::shared_owner<const SceneViewInput>& result, std::string& error);
        // Texture copy commands must precede the native parallel prefix. Uniforms
        // and geometry are prepared afterward in its fresh upload recording.
        bool PrepareResidency(const EnhancedFrameContext& context, const own::shared_owner<const SceneViewInput>& input,
                              std::string& error) const;
        bool Prepare(const EnhancedFrameContext& context, own::shared_owner<const SceneViewInput> input,
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
        // Temporal.Inputs: owns its replay command producer (no culling input).
        std::array<RGHandle, 5> DeclareTemporal(EnhancedRenderGraph& graph, RGHandle geometry,
            std::array<RGHandle, 5> targets) const;
        // Stage contract: each stage returns the handle it publishes and each
        // consumer takes that exact handle (live blackboard slots). Empty frames
        // publish invalid handles. A wrong or missing handle throws.
        // METEOR.Scene.Geometry: one world-transform declaration -> frame geometry table.
        RGHandle DeclareGeometry(EnhancedRenderGraph& graph) const;
        // METEOR.Scene.Cull.First: first camera list + depth commands.
        SceneFirstCullOutputs DeclareFirstCull(EnhancedRenderGraph& graph, RGHandle geometry) const;
        // METEOR.Scene.Occluder: hybrid HW/SW raster of the first list -> occluder depth.
        RGHandle DeclareOccluder(EnhancedRenderGraph& graph, RGHandle geometry, const SceneFirstCullOutputs& first) const;
        // METEOR.Scene.Cull.Final: current HZB, instance/recovery culling and the
        // surface, layer and blended command producers -> final list.
        SceneFinalCullOutputs DeclareFinalCull(EnhancedRenderGraph& graph, RGHandle geometry, RGHandle occluderDepth) const;
        void DeclareHistory(EnhancedRenderGraph& graph, RGHandle depth) const;
        // METEOR.Scene.Shadow: shadow meshlet lists, shadow commands and cascades.
        RGHandle DeclareShadow(EnhancedRenderGraph& graph, RGHandle geometry, RGHandle shadowMap) const;
        // METEOR.Scene.Surface: final hybrid visibility, then material reconstruction into the GBuffer.
        SceneSurfaceTargets::Outputs DeclareGBuffer(EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                                    const SceneSurfaceTargets::Outputs& inputs) const;
        // Baseline is the existing Decal pass snapshot, ordered diffuse, ORM, normal.
        // Register after Decal.Declare and before Color; unchanged channels retain
        // the graph's full precision values instead of the quantized GBuffer.
        void DeclareDecalInputs(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                                const std::array<RGHandle, 3>& baseline) const;
        // METEOR owns the first HDR writer, including empty/pending material frames.
        static RGHandle DeclareHdrTarget(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
        RGHandle DeclareColor(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs, const SceneCommandInputs& commands, RGHandle lighting,
                              RGHandle ambientOcclusion, RGHandle shadowMap) const;
        struct ForwardDraw
        {
            std::size_t index{}, geometryKey{};
            float depth{};
        };
        std::vector<ForwardDraw> BlendedDraws() const;
        RGHandle DeclareBlended(EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices, const SceneCommandInputs& commands, RGHandle lighting, RGHandle shadowMap,
                                const EnhancedForwardLighting& forward) const;
        // METEOR.Scene.Blended: every blended draw back to front over the composed
        // opaque scene, lit through the frame's light tiles.
        RGHandle DeclareBlendedStream(EnhancedRenderGraph& graph, const SceneCommandInputs& commands, RGHandle lighting, RGHandle shadowMap,
                                      const EnhancedForwardLighting& tiles) const;
        RGHandle DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
        std::shared_ptr<const SceneFrameGeometry> FrameGeometry() const;
        std::shared_ptr<const SceneMaterialWork> MaterialWork() const;
        std::shared_ptr<const SceneHybridVisibility> HybridVisibility() const;
        std::shared_ptr<const SceneHybridVisibility> OccluderVisibility() const;
        bool HasDraws() const;
        // Live callers provide the exact graph batch ticket; pending completion is
        // polled without waiting. An empty ticket requires prior successful CPU drain.
        bool PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                                   RHISubmissionTicket ticket = {});
        RHIBufferHandle LookupStatistics() const;
        RGHandle GraphLookupStatistics(const EnhancedRenderGraph& graph) const;
        std::shared_ptr<const SceneLookupFrame> LookupFrame() const;
        std::shared_ptr<const SceneLookupFrame> ForwardLookupFrame() const;
        SceneSurfaceTargets::Outputs ForwardSurfaceOutputs() const;
        std::shared_ptr<const SceneSubsurfaceFrame> SubsurfaceFrame() const;
        std::shared_ptr<const SceneRefractionFrame> RefractionFrame() const;
        std::shared_ptr<const SceneVolumeFrame> VolumeFrame() const;
        // Release after graph/submission owners have drained and before the device.
        void ShutdownAfterIdle();

      private:
        struct Program;
        struct Frame;
        struct GeometryStage;
        struct FirstCullStage;
        struct OccluderStage;
        struct FinalCullStage;
        struct ShadowStage;
        struct TemporalStage;
        struct SurfaceStage;
        struct HistoryStage;
        struct EffectStage;

        struct Preparation;
        struct Slot;
        using SlotKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
        struct Recording
        {
            std::vector<own::shared_owner<const Frame>> owners;
            own::shared_owner<const Frame> publication;
            RHISubmissionTicket ticket;
            std::uint64_t completion{};
            bool submitted{}, accepted{}, decided{};
        };
        bool PrepareProgram(const EnhancedFrameContext& context, const Instance& instance,
                            own::shared_owner<const Program>& result, std::string& error);
        void PruneFailedPreparations(const own::shared_owner<const Generation>& requested = {});
        IRenderDeviceServices* device_{};
        MeshSurfaceEvaluator geometry_;
        GpuGeometryVisibility visibility_;
        GpuGeometryOcclusion cameraOcclusion_;
        struct CameraHistory
        {
            std::shared_ptr<GpuGeometryOcclusion::History> pyramid;
            FrameCameraSnapshot camera;
            std::uint64_t temporalRevision{};
        };
        std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<CameraHistory>> cameraHistories_;
        RenderBindingCache bindings_;
        LX::Runtime::ComputePipeline indirectCommands_;
        HybridVisibilityPipelines hybridPipelines_;
        std::array<LX::Runtime::ComputePipeline,4> materialWorkPipelines_;
        SceneLookupCache lookup_;
        SceneRuntimeEffectsResources runtimeEffects_;
        SceneSubsurfaceResources subsurface_;
        SceneRefractionResources refraction_;
        SceneVolumeResources volume_;
        std::vector<own::shared_owner<const Program>> programs_;
        own::shared_owner<const Frame> frame_;
        job_scheduler& scheduler_;
        IRenderDeviceServices* programDevice_{};
        std::vector<own::unique_owner<Preparation>> preparations_;
        std::map<SlotKey, own::unique_owner<Slot>> slots_;
        bool selectionDeferred_{};
        std::map<std::uint64_t, Recording> recordings_;
        mutable std::mutex recordingMutex_;
        std::uint64_t completed_{}, selectionSerial_{};
        SceneProgramStats stats_;
        bool CommitSubmittedFrame(const own::shared_owner<const Frame>& frame, RHICompletionPoint completion,
                                  std::string& error);
        void PollSubmittedFrames();
        // One HW producer per hybrid stage: occluder (first list, depth commands)
        // or final surface (recovered union, surface commands).
        static void DeclareHardwareVisibility(EnhancedRenderGraph& graph, const own::shared_owner<const Frame>& frame,
                                              bool occluder, RGHandle commands);
        void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
        void OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion) override;
        void OnUploadCompleted(std::uint64_t completed) override;
        void OnUploadAborted(std::uint64_t recording) override;
        void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint reservedCompletion) override;
    };
} // namespace material_graph
