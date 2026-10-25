#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

// METEOR — Material Expression & Topology Editor for Optimized Rendering.
// The scene renderer that draws LX materials: geometry transform, culling,
// shadow/surface raster, colour, blended stream and media. Pass and resource
// names use the METEOR prefix. LX (Lattice eXperiment) is the node language
// itself (LX::Runtime, LX materials) and keeps its own name.

#include "MaterialGraphSceneInput.h"
#include "MaterialGraphFrameGeometry.h"
#include "MaterialGraphSceneGpuData.h"
#include "MaterialGraphWork.h"
#include "MaterialGraphPrimitiveCache.h"
#include "MaterialGraphHybridVisibility.h"
#include "MaterialGraphRenderBindings.h"
#include "MaterialGraphPassReadPlan.h"
#include "MaterialGraphSceneLookup.h"
#include "MaterialGraphSceneRuntimeEffects.h"
#include "MaterialGraphSceneSubsurface.h"
#include "MaterialGraphSceneRefraction.h"
#include "MaterialGraphSceneVolume.h"
#include "GpuGeometryVisibility.h"
#include "GpuGeometryOcclusion.h"
#include "Render/Passes/Geometry/SceneSurfaceTargets.h"
#include "Render/Graph/EnhancedForwardLighting.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Concurrency.Jobs.h"
#else
import Creator.Concurrency.Jobs;
#endif
#include "RHI/RHISubmissionThread.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
// Exact evaluated-pixel cache. Work and memory scale with the viewport; a cold
// cache is still a reference integrator and needs a separate real-time cost gate.
struct SceneHostBudget
{
    assets::cluster::StreamBudget clusterStreaming;
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
    std::uint64_t shadowCacheBytes = 512ull << 20;
    // 실제 light-space recovery가 첫 단계 오판을 복구한다. 네이티브 A/B 수용 전에는 명시적으로만 켠다.
    bool m_shadowDepthPrediction{};
    // 고정 cache의 추가 pass/메모리 비용은 네이티브 A/B로 수용하기 전까지 opt-in으로 둔다.
    bool m_primitiveCache{};
};

struct SceneProgramStats
{
    std::uint64_t compileSubmissions{}, workerExecutions{}, failedPreparations{}, publications{}, stalePublications{},
        failedSubmissions{};
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

// Producer lists and consumer work have separate graph contracts.
struct SceneFirstCullOutputs
{
    RGHandle list;
};
using SceneShadowResources = std::array<RGHandle, 3>;
struct SceneFinalCullOutputs
{
    RGHandle list, blendedList;
};
struct SceneLayerWork
{
    RGHandle layer;
    // Declaration handles borrow graph/blackboard storage through its current
    // declaration epoch. Validate this token before reading any span element.
    std::span<const RGHandle> blended;
    const EnhancedRenderGraph* declarationGraph{};
    uint64_t declarationEpoch{};
    void CheckCurrent(const EnhancedRenderGraph& graph) const;
};
struct SceneColorInputs
{
    RGHandle layer;
    std::array<RGHandle, 2> material;
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
    // Shadow statistics are read only after the graph recording join. Each
    // recording job owns its counters; these accessors reduce the completed jobs.
    // Submitted commands, not a GPU-visible draw count.
    uint32_t ShadowDrawCount() const;
    // Actual indirect submissions: opaque tile packets + ordered Temporal layers.
    uint32_t TemporalBucketSubmissionCount() const;
    uint32_t BlendedBucketSubmissionCount() const;
    std::array<uint32_t, 2> LayerBucketCounts() const;
    // Actual ExecuteIndirect calls, recorded after successful bucket submission.
    std::array<uint32_t, 3> ShadowBucketSubmissionCounts() const;
    // Logical mesh commands per cascade, before GPU culling.
    // A bucket can contain multiple commands in one native submission.
    std::array<uint32_t, 3> RecordedShadowMeshletDrawCounts() const;
    struct ShadowMeshletOutput
    {
        unsigned cascade{}, bin{};
        own::shared_owner<const GpuGeometryVisibility::Frame> frame;
    };
    // Final light-space selection buffers. Static depth can be supplied by the
    // cache instead of replaying these lists; see CurrentShadowCacheStats.
    std::vector<ShadowMeshletOutput> ShadowMeshletOutputs() const;
    own::shared_owner<const GpuGeometryVisibility::Frame> ShadowFirstMeshletWork(unsigned cascade) const;
    bool UsesShadowDepthPrediction() const;

    // Prepared lattice mesh draw bins, before GPU visibility rejection.
    uint32_t PreparedMeshletDrawCount() const;
    // Read only after graph recording joins. Entries are appended by the
    // final METEOR.Scene.Visibility.HW callback after its indirect submission;
    // shadow, occluder and Temporal passes are excluded.
    void EnableGeometryRouteAudit(bool enabled) const;
    SceneGeometryRouteAudit RecordedGeometryRoutes() const;
    // Prepared caster bins per cascade. GPU survivor counts are not read back here.
    std::array<uint32_t, 3> ShadowCasterCounts() const;
    // CPU-prepared camera meshlet work (opaque union + ordered blended list),
    // not GPU-visible counts.
    GpuGeometryVisibility::PreparedStats CameraVisibilityStats() const;
    // Diagnostic owner for submitted GPU work-list/argument readback gates.
    own::shared_owner<const GpuGeometryVisibility::Frame> CameraMeshletWork() const;
    own::shared_owner<const GpuGeometryVisibility::Frame> CameraDepthMeshletWork() const;
    std::array<GpuGeometryVisibility::PreparedStats, 3> ShadowVisibilityStats() const;
    MeshSurfaceCacheStats GeometryStats() const { return geometry_.CacheStats(); }
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
    bool SelectionDeferred() const { return selectionDeferred_; }
    // Temporal.Inputs: opaque visibility/tile restoration; ordered layer replay only.
    std::array<RGHandle, 5> DeclareTemporal(EnhancedRenderGraph& graph, RGHandle visibility,
                                            const std::array<RGHandle, 2>& material, RGHandle layerList,
                                            std::array<RGHandle, 5> targets) const;
    // Stage contract: each stage returns the handle it publishes and each
    // consumer takes that exact handle (live blackboard slots). Empty frames
    // publish invalid handles. A wrong or missing handle throws.
    // METEOR.Scene.Geometry: unique posed-stream producers -> frame geometry table.
    RGHandle DeclareGeometry(EnhancedRenderGraph& graph) const;
    // METEOR.Scene.Cull.First publishes only the first camera list.
    SceneFirstCullOutputs DeclareFirstCull(EnhancedRenderGraph& graph, RGHandle geometry) const;
    // METEOR.Scene.Occluder: hybrid HW/SW raster of the first list -> occluder depth.
    RGHandle DeclareOccluder(EnhancedRenderGraph& graph, RGHandle geometry, const SceneFirstCullOutputs& first) const;
    // METEOR.Scene.Cull.Final publishes recovered opaque and ordered layer lists.
    SceneFinalCullOutputs DeclareFinalCull(EnhancedRenderGraph& graph, RGHandle geometry, RGHandle occluderDepth) const;
    SceneLayerWork DeclareLayerWork(EnhancedRenderGraph& graph, RGHandle layerList) const;
    void DeclareHistory(EnhancedRenderGraph& graph, RGHandle depth) const;
    // Standalone-fixture composition of the six explicit shadow producers below.
    RGHandle DeclareShadow(EnhancedRenderGraph& graph, RGHandle geometry, RGHandle shadowMap,
                           RGHandle cameraOccluderDepth = {}) const;
    SceneShadowResources DeclareShadowStaticCache(EnhancedRenderGraph& graph, RGHandle geometry) const;
    // 예측 모드는 이 프레임 카메라 Occluder의 정확한 버전만 받는다. 최종 그림자 깊이는 입력이 아니다.
    SceneShadowResources DeclareShadowFirstCull(EnhancedRenderGraph& graph, RGHandle geometry,
                                                RGHandle cameraOccluderDepth = {}) const;
    SceneShadowResources DeclareShadowOccluder(EnhancedRenderGraph& graph, RGHandle geometry,
                                               const SceneShadowResources& cache,
                                               const SceneShadowResources& first) const;
    SceneShadowResources DeclareShadowFinalCull(EnhancedRenderGraph& graph, RGHandle geometry,
                                                const SceneShadowResources& depths) const;
    RGHandle DeclareShadowRaster(EnhancedRenderGraph& graph, RGHandle geometry, const SceneShadowResources& cache,
                                 const SceneShadowResources& lists, RGHandle shadowMap) const;
    void DeclareShadowHistory(EnhancedRenderGraph& graph, RGHandle shadowMap) const;
    struct ShadowCacheStats
    {
        bool hit{};
        std::uint32_t staticCasters{}, dynamicCasters{};
        std::uint64_t sourceFrame{}, bytes{};
    };
    ShadowCacheStats CurrentShadowCacheStats() const;
    // Independent graph producers; consumers must pass the published versions.
    SceneSurfaceTargets::Outputs DeclareVisibility(EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                                   const SceneSurfaceTargets::Outputs& targets) const;
    std::array<RGHandle, 2> DeclareMaterialWork(EnhancedRenderGraph& graph, RGHandle geometry,
                                                RGHandle visibility) const;
    SceneSurfaceTargets::Outputs DeclareSurfaceResolve(EnhancedRenderGraph& graph, RGHandle geometry,
                                                       const SceneSurfaceTargets::Outputs& targets,
                                                       const std::array<RGHandle, 2>& material) const;
    // Convenience composition for standalone rendering fixtures, using the same producers.
    SceneSurfaceTargets::Outputs DeclareGBuffer(EnhancedRenderGraph& graph, const SceneFinalCullOutputs& cull,
                                                const SceneSurfaceTargets::Outputs& inputs) const;
    // METEOR owns the first HDR writer, including empty/pending material frames.
    static RGHandle DeclareHdrTarget(EnhancedRenderGraph& graph, const EnhancedFrameContext& context);
    RGHandle DeclareColor(EnhancedRenderGraph& graph, const SceneSurfaceTargets::Outputs& inputs,
                          const SceneColorInputs& commands, RGHandle lighting, RGHandle ambientOcclusion,
                          RGHandle shadowMap) const;
    struct ForwardDraw
    {
        std::size_t index{}, geometryKey{};
        float depth{};
    };
    std::vector<ForwardDraw> BlendedDraws() const;
    RGHandle DeclareBlended(EnhancedRenderGraph& graph, const std::vector<std::size_t>& indices,
                            const SceneLayerWork& commands, RGHandle lighting, RGHandle shadowMap,
                            const EnhancedForwardLighting& forward) const;
    // METEOR.Scene.Blended: every blended draw back to front over the composed
    // opaque scene, lit through the frame's light tiles.
    RGHandle DeclareBlendedStream(EnhancedRenderGraph& graph, const SceneLayerWork& commands, RGHandle lighting,
                                  RGHandle shadowMap, const EnhancedForwardLighting& tiles) const;
    RGHandle DeclareVolume(EnhancedRenderGraph& graph, RGHandle lighting, RGHandle depth, RGHandle shadowMap) const;
    own::shared_owner<const SceneFrameGeometry> FrameGeometry() const;
    own::shared_owner<const SceneMaterialWork> MaterialWork() const;
    own::shared_owner<const SceneHybridVisibility> HybridVisibility() const;
    own::shared_owner<const SceneHybridVisibility> OccluderVisibility() const;
    bool HasDraws() const;
    // Live callers provide the exact graph batch ticket; pending completion is
    // polled without waiting. An empty ticket requires prior successful CPU drain.
    bool PublishSubmittedCache(std::uint64_t frameId, RHICompletionPoint completion, std::string& error,
                               RHISubmissionTicket ticket = {});
    RHIBufferHandle LookupStatistics() const;
    RGHandle GraphLookupStatistics(const EnhancedRenderGraph& graph) const;
    own::shared_owner<const SceneLookupFrame> LookupFrame() const;
    own::shared_owner<const SceneLookupFrame> ForwardLookupFrame() const;
    SceneSurfaceTargets::Outputs ForwardSurfaceOutputs() const;
    own::shared_owner<const SceneSubsurfaceFrame> SubsurfaceFrame() const;
    own::shared_owner<const SceneRefractionFrame> RefractionFrame() const;
    own::shared_owner<const SceneVolumeFrame> VolumeFrame() const;
    // Release after graph/submission owners have drained and before the device.
    void ShutdownAfterIdle();

  private:
    struct Program;
    struct FrameInput;
    struct Frame;
    struct RasterResources;
    struct LayerStage;
    struct VolumeStage;
    struct GeometryStage;
    struct FirstCullStage;
    struct OccluderStage;
    struct FinalCullStage;
    struct ShadowCache;
    struct ShadowCacheStage;
    struct ShadowFirstCullStage;
    struct ShadowOccluderStage;
    struct ShadowFinalCullStage;
    struct ShadowRasterStage;
    struct ShadowHistoryStage;
    struct TemporalStage;
    struct VisibilityStage;
    struct MaterialWorkStage;
    struct SurfaceResolveStage;
    struct HistoryStage;
    struct EffectStage;

    struct Preparation;
    struct PreparationScratch;
    struct Slot;
    using SlotKey = std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>;
    void DeclareSharedFirst(EnhancedRenderGraph& graph, RGHandle geometry) const;
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
    std::array<own::shared_owner<const PassReadPlan>, 8> m_passReadPlans;
    size_t m_nextPassReadPlan{};
    MeshSurfaceEvaluator geometry_;
    GpuGeometryVisibility visibility_;
    GpuGeometryOcclusion cameraOcclusion_;
    GpuGeometryOcclusion shadowOcclusion_;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<own::shared_owner<ShadowCache>>> shadowCaches_;
    std::vector<own::weak_owner<ShadowCache>> shadowCacheAllocations_;
    struct ShadowHistory
    {
        std::array<own::shared_owner<GpuGeometryOcclusion::History>, 3> cascades;
        std::uint64_t revision{}, temporalRevision{}, frameId{};
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<ShadowHistory>> shadowHistories_;
    // Last bucket shape written to stdout; the line is printed only when it changes.
    std::optional<std::pair<std::uint32_t, std::size_t>> loggedMaterialBuckets_;
    // Per-frame scene buffers return here after their frame retires.
    own::shared_owner<TransientBufferPool> bufferPool_;
    ResidentBufferCache sceneResidentBuffers_;
    struct CameraHistory
    {
        own::shared_owner<GpuGeometryOcclusion::History> pyramid;
        FrameCameraSnapshot camera;
        std::uint64_t temporalRevision{};
    };
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::vector<CameraHistory>> cameraHistories_;
    RenderBindingCache bindings_;
    LX::Runtime::ComputePipeline indirectCommands_;
    std::array<LX::Runtime::ComputePipeline, 6> packetPipelines_;
    HybridVisibilityPipelines hybridPipelines_;
    LX::Runtime::GraphicsPipeline shadowMerge_;
    std::array<LX::Runtime::ComputePipeline, 6> materialWorkPipelines_;
    own::shared_owner<ScenePrimitiveCacheResources> m_primitiveCacheResources;
    SceneLookupCache lookup_;
    SceneRuntimeEffectsResources runtimeEffects_;
    SceneSubsurfaceResources subsurface_;
    SceneRefractionResources refraction_;
    SceneVolumeResources volume_;
    std::vector<own::shared_owner<const Program>> programs_;
    own::shared_owner<const Frame> frame_;
    job_scheduler& scheduler_;
    own::unique_owner<PreparationScratch> m_preparationScratch;
    std::atomic_flag m_preparing{};
    IRenderDeviceServices* programDevice_{};
    std::vector<own::unique_owner<Preparation>> preparations_;
    std::map<SlotKey, own::unique_owner<Slot>> slots_;
    bool selectionDeferred_{};
    std::map<std::uint64_t, Recording> recordings_;
    mutable std::mutex recordingMutex_;
    std::uint64_t completed_{}, selectionSerial_{};
    SceneProgramStats stats_;
    std::set<std::pair<std::uint64_t, bool>> loggedPlacementPhases_;
    bool CommitSubmittedFrame(const own::shared_owner<const Frame>& frame, RHICompletionPoint completion,
                              std::string& error);
    void PollSubmittedFrames();
    // One HW producer per hybrid stage: occluder (first list, depth commands)
    // or final surface (recovered union, surface commands).
    static void DeclareHardwareVisibility(EnhancedRenderGraph& graph, const own::shared_owner<const Frame>& frame,
                                          const own::shared_owner<const SceneHybridVisibility>& hybrid,
                                          const own::shared_owner<const GpuGeometryVisibility::Frame>& list,
                                          const own::shared_owner<const IndirectMaterialBatch>& commands, bool occluder,
                                          RGHandle commandInput);
    void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadAccepted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recording) override;
    void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint reservedCompletion) override;
};
} // namespace material_graph
