#pragma once

#include "LXMaterialPipeline.h"
#include "Assets/ClusterStreaming.h"

#include "MaterialGraphSurfaceBatch.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
struct MeshSurfaceBufferPool;
struct MeshSurfaceStaticBuffers;
struct MeshSurfaceStaticCache;
struct MeshSurfaceRequestCache;
struct MeshTransformDispatch;
struct MeshSurfaceCacheStats
{
    std::uint64_t uploads{}, uploadedBytes{}, hits{}, residentBytes{}, entries{};
    std::uint64_t transforms{}, transformHits{}, cachedOutputBytes{}, cachedOutputs{};
};
class MeshSurfacePlan;

// Sealed copy of product ModelAssetGeneration geometry and the draw's pose.
// No Material*, Mesh*, animator pointer or mutable palette survives sealing.
class MeshSurfaceInput
{
  public:
    MeshSurfaceInput(const MeshSurfaceInput&) = delete;
    MeshSurfaceInput& operator=(const MeshSurfaceInput&) = delete;
    MeshSurfaceInput(MeshSurfaceInput&&) = delete;
    MeshSurfaceInput& operator=(MeshSurfaceInput&&) = delete;
    static bool Seal(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                     std::shared_ptr<const MeshSurfaceInput>& result, std::string& error);

    const RHIModelMeshView& Geometry() const { return geometry_; }
    std::span<const std::byte> PackedVertices() const
    {
        return staticOwner_ ? staticOwner_->PackedVertices() : std::span<const std::byte>(packedVertices_);
    }
    const std::shared_ptr<const assets::cluster::Archive>& ClusterArchive() const
    {
        return staticOwner_ ? staticOwner_->ClusterArchive() : clusterArchive_;
    }
    std::shared_ptr<assets::cluster::Stream> ClusterStream() const
    {
        return staticOwner_ ? staticOwner_->ClusterStream() : clusterStream_;
    }
    std::shared_ptr<const assets::cluster::ResidentSet> ResidentClusters() const
    {
        return poseOwner_ ? poseOwner_->ResidentClusters() : residentClusters_;
    }
    const math::matrix4x4& World() const { return world_; }
    const SurfaceView& View() const { return view_; }
    std::span<const PackedBoneMatrix> Bones() const
    {
        return poseOwner_ ? poseOwner_->Bones() : std::span<const PackedBoneMatrix>(bones_);
    }
    std::span<const float> Lods() const { return staticOwner_ ? staticOwner_->Lods() : std::span<const float>(lods_); }
    bool StreamedTopology() const { return staticOwner_ ? staticOwner_->StreamedTopology() : streamedTopology_; }
    std::uint32_t Count() const
    { return staticOwner_ ? staticOwner_->Count() : streamedTopology_ ? logicalVertexCount_ : static_cast<std::uint32_t>(lods_.size()); }
    const experiment::MeshletPayload& Meshlets() const { return staticOwner_ ? staticOwner_->Meshlets() : meshlets_; }
    std::span<const uint32_t> SourceVertexIds() const
    {
        return staticOwner_ ? staticOwner_->SourceVertexIds() : std::span<const uint32_t>(sourceVertexIds_);
    }
    uint32_t SourceVertexCount() const { return staticOwner_ ? staticOwner_->SourceVertexCount() : sourceVertexCount_; }
    uint32_t SourcePrimitiveCount() const
    {
        return staticOwner_ ? staticOwner_->SourcePrimitiveCount() : sourcePrimitiveCount_;
    }
    const experiment::MeshClusterHierarchy& Hierarchy() const
    {
        return staticOwner_ ? staticOwner_->Hierarchy() : hierarchy_;
    }
    uint32_t OriginalIndexCount() const
    {
        return staticOwner_ ? staticOwner_->OriginalIndexCount() : originalIndexCount_;
    }
    bool Matches(const MeshSurfaceInput& other) const;

  private:
    friend class MeshSurfacePlan;
    friend class MeshSurfaceEvaluator;
    MeshSurfaceInput() = default;
    static bool SealImpl(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                         std::uint32_t maxVertices, std::shared_ptr<const MeshSurfaceInput>& result, std::string& error,
                         bool includeHierarchy = false);
    RHIModelMeshView geometry_;
    math::matrix4x4 world_;
    SurfaceView view_;
    std::vector<std::byte> vertices_, packedVertices_;
    std::shared_ptr<const assets::cluster::Archive> clusterArchive_;
    std::shared_ptr<assets::cluster::Stream> clusterStream_;
    std::shared_ptr<const assets::cluster::ResidentSet> residentClusters_;
    std::vector<std::uint32_t> indices_;
    experiment::MeshletPayload meshlets_;
    experiment::MeshClusterHierarchy hierarchy_{};
    uint32_t originalIndexCount_{};
    std::vector<std::uint32_t> sourceVertexIds_;
    uint32_t sourcePrimitiveCount_{}, sourceVertexCount_{};
    uint32_t logicalVertexCount_{};
    bool streamedTopology_{};
    std::vector<PackedBoneMatrix> bones_;
    std::vector<float> lods_;
    // A per-frame pose can share validated, immutable product bytes with the
    // cached partition. The owner outlives all GPU batches retaining this input.
    std::shared_ptr<const MeshSurfaceInput> staticOwner_;
    // Chunk inputs share the sealed current pose, never the cached original pose.
    std::shared_ptr<const MeshSurfaceInput> poseOwner_;
};

struct MeshSurfacePlanBudget
{
    std::uint32_t maxSourceVertices = 1u << 20;
    std::uint32_t maxTriangles = 1u << 21;
    std::uint32_t maxChunkPoints = IblBaker::MaxPoints;
    std::uint32_t maxChunks = 4096;
    std::uint64_t maxCpuPayloadBytes = 256ull << 20;
    std::uint64_t maxGpuPayloadBytes = 512ull << 20;
};

struct MeshSurfacePlanCost
{
    std::uint32_t sourceVertices{}, referencedVertices{}, triangles{};
    std::uint64_t points{}, duplicatedPoints{}, cpuPayloadBytes{}, gpuPayloadBytes{};
    bool sourceReferenceCountsKnown{true};
};

struct MeshSurfaceChunk
{
    std::shared_ptr<const MeshSurfaceInput> input;
    // First-appearance local -> original vertex index. No welding across seams.
    std::span<const std::uint32_t> sourceVertices;
    std::uint32_t firstTriangle{};

  private:
    friend class MeshSurfacePlan;
    std::shared_ptr<const std::vector<std::uint32_t>> sourceVerticesOwner_;
};

// Stable triangle-order partition. Every triangle appears exactly once; shared
// vertices may be duplicated between chunks, retaining the original identity.
class MeshSurfacePlan
{
  public:
    static bool Build(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                      const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                      std::string& error);
    // Scene frames reuse the validated partition of one immutable model mesh.
    // World, eye, revision and bone palette are sealed afresh on every call.
    static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                              const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                              std::string& error);
    // Raster derives mip LOD per pixel; allocate zero vertex transport only on a cache miss.
    static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                              const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                              std::string& error);
    std::span<const MeshSurfaceChunk> Chunks() const { return chunks_; }
    const std::shared_ptr<const MeshSurfaceInput>& Source() const { return source_; }
    const MeshSurfacePlanCost& Cost() const { return cost_; }
    bool Matches(const MeshSurfacePlan& other) const;
    bool SharesSceneTopology(const MeshSurfacePlan& other) const;
    // 불변 partition의 약한 신원이다. pose/frame과 GPU 자원의 수명을 연장하지 않는다.
    std::weak_ptr<const MeshSurfaceInput> SceneTopologyIdentity() const;

  private:
    MeshSurfacePlan() = default;
    static bool BuildPartitioned(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                 const MeshSurfacePlanBudget& budget, std::uint32_t chunkCeiling,
                                 std::shared_ptr<const MeshSurfacePlan>& result, std::string& error,
                                 bool includeHierarchy = false);
    static bool BuildSceneZeroLod(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                  const MeshSurfacePlanBudget& budget, std::shared_ptr<const MeshSurfacePlan>& result,
                                  std::string& error);
    static bool BuildStreamed(const EnhancedDrawItem& draw, const SurfaceView& view, const MeshSurfacePlanBudget& budget,
                              std::shared_ptr<const MeshSurfacePlan>& result, std::string& error);
    std::shared_ptr<const MeshSurfaceInput> source_;
    std::vector<MeshSurfaceChunk> chunks_;
    MeshSurfacePlanCost cost_;
    std::uint32_t chunkLimit_{};
    std::shared_ptr<const MeshSurfacePlan> staticOwner_;
};

struct SurfaceTextureFootprint
{
    std::array<float, 2> uvDx{}, uvDy{};
    std::uint32_t width{}, height{}, mipLevels{};
    float bias{};
};
// Isotropic explicit SampleLevel LOD for one texture footprint. The raster host
// supplies perspective-correct UV derivatives; no compute derivatives are invented.
bool ResolveSurfaceLod(const SurfaceTextureFootprint& footprint, float& lod, std::string& error);

struct MeshSurfaceSample
{
    std::uint32_t triangle{};
    // Perspective-correct weights for vertices B/C; A = 1 - B - C.
    float b{}, c{}, lod{};
};
static_assert(sizeof(MeshSurfaceSample) == 16);

class MeshSurfaceBatch final : public SurfaceGeometrySource
{
  public:
    ~MeshSurfaceBatch() override;
    MeshSurfaceBatch(const MeshSurfaceBatch&) = delete;
    MeshSurfaceBatch& operator=(const MeshSurfaceBatch&) = delete;
    RHIBufferHandle Buffer() const override { return buffer_; }
    const IRenderDeviceServices* Device() const override { return device_; }
    const std::shared_ptr<const MeshSurfaceInput>& Input() const { return input_; }
    const SurfaceView& View() const override { return input_->View(); }
    std::uint32_t Count() const override { return count_; }
    bool IsSampled() const { return !samples_.empty(); }
    std::uint64_t RecordingId() const override { return recordingId_; }
    bool IsValidated() const override { return validated_; }
    bool IsReadyForEvaluation() const override
    {
        return declarationOwner_ ? declared_.load() && declarationOwner_->IsReadyForEvaluation()
                                 : recordedStages_.load() == 3;
    }
    bool IsPreparedForGraph() const override;
    RGHandle GraphOutput(const EnhancedRenderGraph& graph) const override;
    bool HasGraphDeclaration() const { return graph_ != nullptr; }
    // Scene instances prepared together with identical geometry/pose/pipeline
    // share posed object-space output; consumers apply their own ObjectToWorld.
    bool IsObjectSpace() const { return objectSpace_; }
    bool UsesDirectClusters() const { return directClusters_; }
    const std::shared_ptr<const assets::cluster::ResidentSet>& ClusterResidency() const { return clusterResidency_; }
    uint64_t BufferBytes() const { return bufferBytes_ ? bufferBytes_ : uint64_t(count_) * sizeof(SurfacePoint); }
    static constexpr uint32_t GeometryViewStride = sizeof(float) * 4;
    uint32_t GeometryViewElements() const { return static_cast<uint32_t>(BufferBytes() / GeometryViewStride); }
    const math::matrix4x4& ObjectToWorld() const;
    RHIBufferSlice Indices() const;
    RHIBufferSlice Meshlets() const;
    uint32_t MeshletCount() const;
    RGHandle GraphMeshlets(const EnhancedRenderGraph& graph) const;
    bool Declare(EnhancedRenderGraph& graph, std::string& error) const;
    // One transform pass, one readiness pass and one reuse pass for the whole
    // set, so graph and profiler pass counts do not grow with the mesh count.
    static bool DeclareBatch(EnhancedRenderGraph& graph, std::span<const MeshSurfaceBatch* const> batches,
                             std::string& error);
    void MarkSubmitted(RHICompletionPoint completion) const
    {
        completion_ = completion;
        if (declarationOwner_)
            declarationOwner_->MarkSubmitted(completion);
        else if (transformedOwner_)
            transformedOwner_->MarkSubmitted(completion);
    }
    bool ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const override;

  private:
    friend class MeshSurfaceEvaluator;
    MeshSurfaceBatch() = default;
    static bool PrepareDispatch(std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,
                                std::shared_ptr<MeshTransformDispatch>& result, std::string& error);
    static bool RecordDispatch(RHIEncoder& encoder, std::span<const std::shared_ptr<const MeshSurfaceBatch>> batches,
                               const MeshTransformDispatch& dispatch, std::string& error);
    // Imports inputs/output and appends this mesh's usages; adds no pass.
    bool DeclareResources(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& inputs,
                          bool& reused, std::string& error) const;
    IRenderDeviceServices* device_{};
    RHIBufferHandle buffer_;
    uint64_t bufferBytes_{};
    bool directClusters_{};
    RHIBufferSlice clusterPoseUpload_;
    std::shared_ptr<const assets::cluster::ResidentSet> clusterResidency_;
    std::shared_ptr<MeshSurfaceBufferPool> bufferPool_;
    std::shared_ptr<MeshSurfaceStaticBuffers> staticBuffers_;
    // Buffer lifetime belongs to the cached producer. A recording has exactly
    // one declaration owner; instance wrappers retain their own input/world.
    std::shared_ptr<const MeshSurfaceBatch> transformedOwner_;
    std::shared_ptr<const MeshSurfaceBatch> declarationOwner_;
    mutable std::weak_ptr<const MeshSurfaceBatch> reuseDeclaration_;
    mutable std::uint64_t lastUsedGeneration_{};
    mutable RHICompletionPoint completion_{};
    mutable RHIResourceState bufferState_{RHIResourceState::Common};
    std::shared_ptr<const MeshSurfaceInput> input_;
    std::shared_ptr<const MeshSurfaceBatch> source_;
    std::vector<MeshSurfaceSample> samples_;
    std::uint32_t count_{};
    std::uint64_t recordingId_{}, auditRealFrame_{}, auditView_{};
    std::uint64_t descriptorVersion_{};
    std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline_;
    // Immediate sampler ownership must not make this a prepared world transform.
    std::shared_ptr<const LX::Runtime::ComputeGeneration> samplePipeline_;

    std::weak_ptr<const MeshSurfaceBatch> self_;
    mutable std::atomic<unsigned> recordedStages_{};
    mutable std::atomic<bool> declared_{};
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable RGHandle graphOutput_;
    mutable bool validated_{};
    bool objectSpace_{};
};

class MeshSurfaceEvaluator
{
  public:
    // Consume a compiled host artifact. Runtime initialization does not compile.
    bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots, IRenderPipelineCache& pipelines,
                    const RHIShaderBlob& shader, std::string& error,
                    LX::Runtime::ComputeShaderDescription identity = {});
    bool InitializeSampler(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                           IRenderPipelineCache& pipelines, const RHIShaderBlob& shader, std::string& error,
                           LX::Runtime::ComputeShaderDescription identity = {});
    // Immediate preparation before RenderGraph recording. Output is ShaderResource;
    // retain the batch through submission completion and release before shutdown.
    bool Record(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error);
    // Prepare after PrepareParallel, then Declare before raster/material consumers.
    // This records the current pose inside the same graph without an earlier submit.
    bool Prepare(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                 std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error, bool cacheStatic = false,
                 std::uint64_t auditRealFrame = 0, std::uint64_t auditView = 0, bool objectSpace = false,
                 bool directClusters = false, const assets::cluster::StreamBudget& clusterBudget = {});
    void NotifyCompleted(std::uint64_t completed) const;
    // Advance once per upload recording, including empty scene frames. Active
    // outputs have scene lifetime; only unused, completed owners are retired.
    void BeginFrame(std::uint64_t recording);
    MeshSurfaceCacheStats CacheStats() const;
    // Interpolate UV/world frame at exact sample locations BEFORE graph evaluation
    // and IBL bake. This never interpolates already evaluated materials/radiance.
    bool RecordSamples(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceBatch> vertices,
                       std::span<const MeshSurfaceSample> samples, std::shared_ptr<const MeshSurfaceBatch>& result,
                       std::string& error);

  private:
    IRenderDeviceServices* device_{};
    std::shared_ptr<const LX::Runtime::ComputeGeneration> pipeline_;
    std::shared_ptr<const LX::Runtime::ComputeGeneration> samplerPipeline_;
    std::shared_ptr<MeshSurfaceBufferPool> bufferPool_;
    std::shared_ptr<MeshSurfaceStaticCache> staticCache_;
    std::shared_ptr<MeshSurfaceRequestCache> requestCache_;
    std::vector<std::shared_ptr<const MeshSurfaceBatch>> transformedCache_;
    std::uint64_t transformedBytes_{}, transforms_{}, transformHits_{};
    std::uint64_t cacheRecording_{}, cacheGeneration_{};
};
} // namespace material_graph
