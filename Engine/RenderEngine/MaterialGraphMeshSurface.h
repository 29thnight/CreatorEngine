#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "LXMaterialPipeline.h"
#include "Assets/ClusterStreaming.h"

#include "MaterialGraphSurfaceBatch.h"
#include "Render/Graph/EnhancedRenderPass.h"

#include "../Utility_Framework/Foundation/StandardLibrary.h"

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
                         own::shared_owner<const MeshSurfaceInput>& result, std::string& error);

        const RHIModelMeshView& Geometry() const { return geometry_; }
        std::span<const std::byte> PackedVertices() const
        {
            return staticOwner_ ? staticOwner_->PackedVertices() : std::span<const std::byte>(packedVertices_);
        }
        const own::shared_owner<const assets::cluster::Archive>& ClusterArchive() const
        {
            return staticOwner_ ? staticOwner_->ClusterArchive() : clusterArchive_;
        }
        const own::shared_owner<assets::cluster::Stream>& ClusterStream() const
        {
            return staticOwner_ ? staticOwner_->ClusterStream() : clusterStream_;
        }
        const own::shared_owner<const assets::cluster::ResidentSet>& ResidentClusters() const
        {
            return poseOwner_ ? poseOwner_->ResidentClusters() : residentClusters_;
        }
        const math::matrix4x4& World() const { return world_; }
        const SurfaceView& View() const { return view_; }
        std::span<const PackedBoneMatrix> Bones() const
        {
            return poseOwner_ ? poseOwner_->Bones() : std::span<const PackedBoneMatrix>(bones_);
        }
        std::span<const float> Lods() const
        {
            return staticOwner_ ? staticOwner_->Lods() : std::span<const float>(lods_);
        }
        bool StreamedTopology() const { return staticOwner_ ? staticOwner_->StreamedTopology() : streamedTopology_; }
        std::uint32_t Count() const
        {
            return staticOwner_        ? staticOwner_->Count()
                   : streamedTopology_ ? logicalVertexCount_
                                       : static_cast<std::uint32_t>(lods_.size());
        }
        const experiment::MeshletPayload& Meshlets() const
        {
            return staticOwner_ ? staticOwner_->Meshlets() : meshlets_;
        }
        std::span<const uint32_t> SourceVertexIds() const
        {
            return staticOwner_ ? staticOwner_->SourceVertexIds() : std::span<const uint32_t>(sourceVertexIds_);
        }
        uint32_t SourceVertexCount() const
        {
            return staticOwner_ ? staticOwner_->SourceVertexCount() : sourceVertexCount_;
        }
        uint32_t SourcePrimitiveCount() const
        {
            return staticOwner_ ? staticOwner_->SourcePrimitiveCount() : sourcePrimitiveCount_;
        }
        const experiment::MeshClusterHierarchy& Hierarchy() const
        {
            return staticOwner_ ? staticOwner_->Hierarchy() : hierarchy_;
        }
        const math::vector4& InstanceMinimum() const
        {
            return staticOwner_ ? staticOwner_->InstanceMinimum() : m_instanceMinimum;
        }
        const math::vector4& InstanceMaximum() const
        {
            return staticOwner_ ? staticOwner_->InstanceMaximum() : m_instanceMaximum;
        }
        uint32_t OriginalIndexCount() const
        {
            return staticOwner_ ? staticOwner_->OriginalIndexCount() : originalIndexCount_;
        }
        bool Matches(const MeshSurfaceInput& other) const;

      private:
        friend class MeshSurfacePlan;
        friend class MeshSurfaceEvaluator;

      public:
        class ConstructionKey
        {
            friend class MeshSurfaceInput;
            friend class MeshSurfacePlan;
            friend class MeshSurfaceEvaluator;
            ConstructionKey() = default;
        };
        explicit MeshSurfaceInput(ConstructionKey) {}

      private:
        void SealInstanceBounds();
        static bool SealImpl(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                             std::uint32_t maxVertices, own::shared_owner<const MeshSurfaceInput>& result,
                             std::string& error, bool includeHierarchy = false);
        RHIModelMeshView geometry_;
        math::matrix4x4 world_;
        SurfaceView view_;
        std::vector<std::byte> vertices_, packedVertices_;
        own::shared_owner<const assets::cluster::Archive> clusterArchive_;
        own::shared_owner<assets::cluster::Stream> clusterStream_;
        own::shared_owner<const assets::cluster::ResidentSet> residentClusters_;
        std::vector<std::uint32_t> indices_;
        experiment::MeshletPayload meshlets_;
        experiment::MeshClusterHierarchy hierarchy_{};
        // 불변 압축 토폴로지의 경계다. W=0이면 현재 포즈의 조기 컬링을 생략한다.
        math::vector4 m_instanceMinimum{}, m_instanceMaximum{};
        uint32_t originalIndexCount_{};
        std::vector<std::uint32_t> sourceVertexIds_;
        uint32_t sourcePrimitiveCount_{}, sourceVertexCount_{};
        uint32_t logicalVertexCount_{};
        bool streamedTopology_{};
        std::vector<PackedBoneMatrix> bones_;
        std::vector<float> lods_;
        // A per-frame pose can share validated, immutable product bytes with the
        // cached partition. The owner outlives all GPU batches retaining this input.
        own::shared_owner<const MeshSurfaceInput> staticOwner_;
        // Chunk inputs share the sealed current pose, never the cached original pose.
        own::shared_owner<const MeshSurfaceInput> poseOwner_;
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
        own::shared_owner<const MeshSurfaceInput> input;
        // First-appearance local -> original vertex index. No welding across seams.
        std::span<const std::uint32_t> sourceVertices;
        std::uint32_t firstTriangle{};

      private:
        friend class MeshSurfacePlan;
        own::shared_owner<const std::vector<std::uint32_t>> sourceVerticesOwner_;
    };

    // Stable triangle-order partition. Every triangle appears exactly once; shared
    // vertices may be duplicated between chunks, retaining the original identity.
    class MeshSurfacePlan
    {
      public:
        static bool Build(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                          const MeshSurfacePlanBudget& budget, own::shared_owner<const MeshSurfacePlan>& result,
                          std::string& error);
        // Scene frames reuse the validated partition of one immutable model mesh.
        // World, eye, revision and bone palette are sealed afresh on every call.
        static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                  const MeshSurfacePlanBudget& budget, own::shared_owner<const MeshSurfacePlan>& result,
                                  std::string& error);
        // Raster derives mip LOD per pixel; allocate zero vertex transport only on a cache miss.
        static bool BuildForScene(const EnhancedDrawItem& draw, const SurfaceView& view,
                                  const MeshSurfacePlanBudget& budget, own::shared_owner<const MeshSurfacePlan>& result,
                                  std::string& error);
        // Retire session cache ownership; existing frame plans retain their owners.
        static void ReleaseCachedScenePlans();
        std::span<const MeshSurfaceChunk> Chunks() const { return chunks_; }
        const own::shared_owner<const MeshSurfaceInput>& Source() const { return source_; }
        const MeshSurfacePlanCost& Cost() const { return cost_; }
        bool Matches(const MeshSurfacePlan& other) const;
        bool SharesSceneTopology(const MeshSurfacePlan& other) const;
        // 불변 partition의 약한 신원이다. pose/frame과 GPU 자원의 수명을 연장하지 않는다.
        own::weak_owner<const MeshSurfaceInput> SceneTopologyIdentity() const;

      private:
      public:
        class ConstructionKey
        {
            friend class MeshSurfacePlan;
            ConstructionKey() = default;
        };
        explicit MeshSurfacePlan(ConstructionKey) {}

      private:
        static bool BuildPartitioned(const EnhancedDrawItem& draw, const SurfaceView& view, std::span<const float> lods,
                                     const MeshSurfacePlanBudget& budget, std::uint32_t chunkCeiling,
                                     own::shared_owner<const MeshSurfacePlan>& result, std::string& error,
                                     bool includeHierarchy = false);
        static bool BuildSceneZeroLod(const EnhancedDrawItem& draw, const SurfaceView& view,
                                      std::span<const float> lods, const MeshSurfacePlanBudget& budget,
                                      own::shared_owner<const MeshSurfacePlan>& result, std::string& error);
        static bool BuildStreamed(const EnhancedDrawItem& draw, const SurfaceView& view,
                                  const MeshSurfacePlanBudget& budget, own::shared_owner<const MeshSurfacePlan>& result,
                                  std::string& error);
        own::shared_owner<const MeshSurfaceInput> source_;
        std::vector<MeshSurfaceChunk> chunks_;
        MeshSurfacePlanCost cost_;
        std::uint32_t chunkLimit_{};
        own::shared_owner<const MeshSurfacePlan> staticOwner_;
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
        const own::shared_owner<const MeshSurfaceInput>& Input() const { return input_; }
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
        const own::shared_owner<const assets::cluster::ResidentSet>& ClusterResidency() const
        {
            return clusterResidency_;
        }
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
            {
                declarationOwner_->MarkSubmitted(completion);
            }
            else if (transformedOwner_)
            {
                transformedOwner_->MarkSubmitted(completion);
            }
        }
        bool ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const override;

      private:
        friend class MeshSurfaceEvaluator;

      public:
        class ConstructionKey
        {
            friend class MeshSurfaceBatch;
            friend class MeshSurfaceEvaluator;
            ConstructionKey() = default;
        };
        explicit MeshSurfaceBatch(ConstructionKey) {}

      private:
        static bool PrepareDispatch(std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                    own::shared_owner<MeshTransformDispatch>& result, std::string& error);
        static bool PrepareDeclarationDispatch(std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                               own::shared_owner<MeshTransformDispatch>& result, std::string& error);
        static bool RecordDispatch(RHIEncoder& encoder,
                                   std::span<const own::shared_owner<const MeshSurfaceBatch>> batches,
                                   const MeshTransformDispatch& dispatch, std::string& error);
        // Imports inputs/output and appends this mesh's usages; adds no pass.
        bool DeclareResources(EnhancedRenderGraph& graph, std::vector<EnhancedRenderGraph::RGPassUsage>& inputs,
                              bool& reused, std::string& error) const;
        IRenderDeviceServices* device_{};
        RHIBufferHandle buffer_;
        uint64_t bufferBytes_{};
        bool directClusters_{};
        RHIBufferSlice clusterPoseUpload_;
        own::shared_owner<const assets::cluster::ResidentSet> clusterResidency_;
        own::shared_owner<MeshSurfaceBufferPool> bufferPool_;
        own::shared_owner<MeshSurfaceStaticBuffers> staticBuffers_;
        // Buffer lifetime belongs to the cached producer. A recording has exactly
        // one declaration owner; instance wrappers retain their own input/world.
        own::shared_owner<const MeshSurfaceBatch> transformedOwner_;
        own::shared_owner<const MeshSurfaceBatch> declarationOwner_;
        mutable own::weak_owner<const MeshSurfaceBatch> reuseDeclaration_;
        mutable std::uint64_t lastUsedGeneration_{};
        mutable RHICompletionPoint completion_{};
        mutable RHIResourceState bufferState_{RHIResourceState::Common};
        own::shared_owner<const MeshSurfaceInput> input_;
        own::shared_owner<const MeshSurfaceBatch> source_;
        std::vector<MeshSurfaceSample> samples_;
        std::uint32_t count_{};
        std::uint64_t recordingId_{}, auditRealFrame_{}, auditView_{};
        std::uint64_t descriptorVersion_{};
        own::shared_owner<const LX::Runtime::ComputeGeneration> pipeline_;
        // Immediate sampler ownership must not make this a prepared world transform.
        own::shared_owner<const LX::Runtime::ComputeGeneration> samplePipeline_;

        own::weak_owner<const MeshSurfaceBatch> self_;
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
        bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                        IRenderPipelineCache& pipelines, const RHIShaderBlob& shader, std::string& error,
                        LX::Runtime::ComputeShaderDescription identity = {});
        bool InitializeSampler(IRenderDeviceServices& device, IRenderRootSignatureCache& roots,
                               IRenderPipelineCache& pipelines, const RHIShaderBlob& shader, std::string& error,
                               LX::Runtime::ComputeShaderDescription identity = {});
        // Immediate preparation before RenderGraph recording. Output is ShaderResource;
        // retain the batch through submission completion and release before shutdown.
        bool Record(IRenderDeviceServices& device, own::shared_owner<const MeshSurfaceInput> input,
                    own::shared_owner<const MeshSurfaceBatch>& result, std::string& error);
        // Prepare after PrepareParallel, then Declare before raster/material consumers.
        // This records the current pose inside the same graph without an earlier submit.
        bool Prepare(IRenderDeviceServices& device, own::shared_owner<const MeshSurfaceInput> input,
                     own::shared_owner<const MeshSurfaceBatch>& result, std::string& error, bool cacheStatic = false,
                     std::uint64_t auditRealFrame = 0, std::uint64_t auditView = 0, bool objectSpace = false,
                     bool directClusters = false, const assets::cluster::StreamBudget& clusterBudget = {});
        void NotifyCompleted(std::uint64_t completed) const;
        // Advance once per upload recording, including empty scene frames. Active
        // outputs have scene lifetime; only unused, completed owners are retired.
        void BeginFrame(std::uint64_t recording);
        MeshSurfaceCacheStats CacheStats() const;
        // Interpolate UV/world frame at exact sample locations BEFORE graph evaluation
        // and IBL bake. This never interpolates already evaluated materials/radiance.
        bool RecordSamples(IRenderDeviceServices& device, own::shared_owner<const MeshSurfaceBatch> vertices,
                           std::span<const MeshSurfaceSample> samples,
                           own::shared_owner<const MeshSurfaceBatch>& result, std::string& error);

      private:
        IRenderDeviceServices* device_{};
        own::shared_owner<const LX::Runtime::ComputeGeneration> pipeline_;
        own::shared_owner<const LX::Runtime::ComputeGeneration> samplerPipeline_;
        own::shared_owner<MeshSurfaceBufferPool> bufferPool_;
        own::shared_owner<MeshSurfaceStaticCache> staticCache_;
        own::shared_owner<MeshSurfaceRequestCache> requestCache_;
        std::vector<own::shared_owner<const MeshSurfaceBatch>> transformedCache_;
        std::uint64_t transformedBytes_{}, transforms_{}, transformHits_{};
        std::uint64_t cacheRecording_{}, cacheGeneration_{};
    };
} // namespace material_graph
