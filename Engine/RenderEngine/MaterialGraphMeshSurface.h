#pragma once

#include "MaterialGraphSurfaceBatch.h"
#include "Render/Graph/EnhancedRenderPass.h"

namespace material_graph
{
struct MeshSurfaceBufferPool;
struct MeshSurfaceStaticBuffers;
struct MeshSurfaceStaticCache;
struct MeshSurfaceCacheStats
{
    std::uint64_t uploads{}, uploadedBytes{}, hits{}, residentBytes{}, entries{};
    std::uint64_t transforms{}, transformHits{}, cachedOutputBytes{};
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
    const math::matrix4x4& World() const { return world_; }
    const SurfaceView& View() const { return view_; }
    std::span<const PackedBoneMatrix> Bones() const { return poseOwner_ ? poseOwner_->Bones() : std::span<const PackedBoneMatrix>(bones_); }
    std::span<const float> Lods() const { return staticOwner_ ? staticOwner_->Lods() : std::span<const float>(lods_); }
    std::uint32_t Count() const { return static_cast<std::uint32_t>(Lods().size()); }
    bool Matches(const MeshSurfaceInput& other) const;

  private:
    friend class MeshSurfacePlan;
    friend class MeshSurfaceEvaluator;
    MeshSurfaceInput() = default;
    static bool SealImpl(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                         std::uint32_t maxVertices, std::shared_ptr<const MeshSurfaceInput>& result,
                         std::string& error);
    RHIModelMeshView geometry_;
    math::matrix4x4 world_;
    SurfaceView view_;
    std::vector<std::byte> vertices_;
    std::vector<std::uint32_t> indices_;
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
};

struct MeshSurfaceChunk
{
    std::shared_ptr<const MeshSurfaceInput> input;
    // First-appearance local -> original vertex index. No welding across seams.
    std::vector<std::uint32_t> sourceVertices;
    std::uint32_t firstTriangle{};
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
    static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                              std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                              std::shared_ptr<const MeshSurfacePlan>& result, std::string& error);
    // Raster derives mip LOD per pixel; allocate zero vertex transport only on a cache miss.
    static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                              const MeshSurfacePlanBudget& budget,
                              std::shared_ptr<const MeshSurfacePlan>& result, std::string& error);
    std::span<const MeshSurfaceChunk> Chunks() const { return chunks_; }
    const std::shared_ptr<const MeshSurfaceInput>& Source() const { return source_; }
    const MeshSurfacePlanCost& Cost() const { return cost_; }
    bool Matches(const MeshSurfacePlan& other) const;

  private:
    MeshSurfacePlan() = default;
    static bool BuildPartitioned(const EnhancedDrawItem& draw, const SurfaceView& view,
                                 std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                 std::uint32_t chunkCeiling,
                                 std::shared_ptr<const MeshSurfacePlan>& result, std::string& error);
    static bool BuildSceneZeroLod(const EnhancedDrawItem& draw, const SurfaceView& view,
                                 std::span<const float> lods, const MeshSurfacePlanBudget& budget,
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
    bool IsReadyForEvaluation() const override { return recordedStages_.load() == 3; }
    bool IsPreparedForGraph() const override;
    RGHandle GraphOutput(const EnhancedRenderGraph& graph) const override;
    bool HasGraphDeclaration() const { return graph_ != nullptr; }
    RHIBufferSlice Indices() const;
    bool Declare(EnhancedRenderGraph& graph, std::string& error) const;
    void MarkSubmitted(RHICompletionPoint completion) const { completion_ = completion; }
    bool ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const override;

  private:
    friend class MeshSurfaceEvaluator;
    MeshSurfaceBatch() = default;
    bool RecordCommands(RHIEncoder& encoder, std::string& error) const;
    IRenderDeviceServices* device_{};
    RHIBufferHandle buffer_;
    std::shared_ptr<MeshSurfaceBufferPool> bufferPool_;
    std::shared_ptr<MeshSurfaceStaticBuffers> staticBuffers_;
    // Reuse only immutable, completed outputs. Recording/descriptors stay local.
    std::shared_ptr<const MeshSurfaceBatch> transformedOwner_;
    mutable std::uint64_t lastReuseRecording_{};
    mutable RHICompletionPoint completion_{};
    mutable RHIResourceState bufferState_{RHIResourceState::Common};
    std::shared_ptr<const MeshSurfaceInput> input_;
    std::shared_ptr<const MeshSurfaceBatch> source_;
    std::vector<MeshSurfaceSample> samples_;
    std::uint32_t count_{};
    std::uint64_t recordingId_{};
    std::uint64_t descriptorVersion_{};
    RHIPipelineHandle pipeline_;
    RHIBufferSlice vertices_, bones_, lods_, uniform_;
    RHIBindingTable output_;
    std::weak_ptr<const MeshSurfaceBatch> self_;
    mutable std::atomic<unsigned> recordedStages_{};
    mutable std::atomic<bool> declared_{};
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable RGHandle graphOutput_;
    mutable bool validated_{};
};

class MeshSurfaceEvaluator
{
  public:
    // Consume a compiled host artifact. Runtime initialization does not compile.
    bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots, IRenderPipelineCache& pipelines,
                    const RHIShaderBlob& shader, std::string& error);
    bool InitializeSampler(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                           IRenderPipelineCache& pipelines, const RHIShaderBlob& shader, std::string& error);
    // Immediate preparation before RenderGraph recording. Output is ShaderResource;
    // retain the batch through submission completion and release before shutdown.
    bool Record(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error);
    // Prepare after PrepareParallel, then Declare before raster/material consumers.
    // This records the current pose inside the same graph without an earlier submit.
    bool Prepare(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceInput> input,
                 std::shared_ptr<const MeshSurfaceBatch>& result, std::string& error, bool cacheStatic = false);
    void NotifyCompleted(std::uint64_t completed) const;
    MeshSurfaceCacheStats CacheStats() const;
    // Interpolate UV/world frame at exact sample locations BEFORE graph evaluation
    // and IBL bake. This never interpolates already evaluated materials/radiance.
    bool RecordSamples(IRenderDeviceServices& device, std::shared_ptr<const MeshSurfaceBatch> vertices,
                       std::span<const MeshSurfaceSample> samples, std::shared_ptr<const MeshSurfaceBatch>& result,
                       std::string& error);

  private:
    IRenderDeviceServices* device_{};
    RHIPipelineHandle pipeline_;
    RHIPipelineHandle samplerPipeline_;
    std::shared_ptr<MeshSurfaceBufferPool> bufferPool_;
    std::shared_ptr<MeshSurfaceStaticCache> staticCache_;
    std::vector<std::shared_ptr<const MeshSurfaceBatch>> transformedCache_;
    std::uint64_t transformedBytes_{}, transforms_{}, transformHits_{};
};
} // namespace material_graph
