#pragma once

#include "LXMaterialPipeline.h"
#include "GpuGeometryOcclusion.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector4.hpp>

#include <cstdint>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct EnhancedFrameContext;
namespace material_graph { class MeshSurfaceBatch; }

// A recording owns its candidates, compacted IDs and indirect arguments. Buffers
// are never overwritten by another view/frame or released while submitted work
// can still read them. Call ShutdownAfterIdle before destroying the device.
class GpuGeometryVisibility final : private IRHIUploadTransactionListener
{
    struct BufferPool;
  public:
    static constexpr std::uint32_t kConservative = 1;
    static constexpr std::uint32_t kNoOcclusion = 2;
    static constexpr std::uint32_t kMeshletOwner = 4;
    // Root storage-buffer offsets must satisfy both backends' alignment limits.
    // Units are uint32 IDs, so each bin starts at a 256-byte boundary.
    static constexpr std::uint32_t kOutputAlignment = 64;
    static constexpr std::uint32_t kOcclusionLevels = GpuGeometryOcclusion::kMaximumLevels;

    struct Candidate
    {
        // World-space center/radius; a nonpositive or nonfinite radius is unknown.
        math::vector4 sphere{};
        std::uint32_t bin{}, sourceIndex{}, outputOffset{}, flags{};
    };
    static_assert(sizeof(Candidate) == 32);

    struct Bin
    {
        std::uint32_t indexCount{}, firstIndex{};
        std::int32_t baseVertex{};
        // Zero compacts visible IDs. Nonzero preserves the original complete
        // instance stream: count is N or 0, never a reordered subset. Candidates
        // must provide every original sourceIndex 0..N-1 exactly once.
        std::uint32_t preservedInstanceCount{};
    };
    static_assert(sizeof(Bin) == 16);

    // A dedicated mesh draw bin reuses the visibility kernel's first three
    // argument words as DispatchMesh(x=groups, y=visible 0/1, z=1). It must be
    // consumed only by DispatchMeshIndirect, never DrawIndexedIndirect, and
    // must contain exactly one candidate. This preserves camera HZB and the
    // independent shadow selection without CPU readback or another dispatch.
    static Bin MeshDispatchBin(std::uint32_t groups)
    {
        return {groups, 1, 0, 0};
    }


    struct PreparedStats
    {
        std::uint32_t candidateCount{}, compactedBins{}, preservedBins{}, conservativeCandidates{};
    };

    class Frame
    {
      public:
        ~Frame();
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        void Declare(EnhancedRenderGraph& graph) const;
        static void DeclareMeshletBatch(EnhancedRenderGraph& graph,
            std::span<const std::shared_ptr<const Frame>> frames,
            const char* passPrefix = "METEOR.ShadowMeshlet");
        // Optional second geometry stage: depth must be an earlier RG2 version
        // from this exact view/recording, standard-Z (clear 1, Less/LessEqual).
        // It must not be the version written by this frame's geometry consumer.
        // Tested draws must use these bounds/projection without custom pixel
        // depth or depth bias. The METEOR GBuffer caller supplies this contract.
        // No history is kept or reprojected. Call before AddReadUsages/Declare.
        void DeclareWithOcclusion(EnhancedRenderGraph& graph, RGHandle occluderDepth) const;
        void DeclareWithOcclusion(EnhancedRenderGraph& graph,
            std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid) const;
        void DeclareFirstStage(EnhancedRenderGraph& graph) const;
        void AddReadUsages(EnhancedRenderGraph& graph,
                           std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        // Explicit consumers: producer must already belong to this graph recording.
        void AddCommandInputRead(EnhancedRenderGraph& graph,
            std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        void AddVisibleIdsRead(EnhancedRenderGraph& graph,
            std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        RHIBufferSlice VisibleIds(std::uint32_t outputOffset, std::uint32_t count) const;
        RHIBufferHandle VisibleIdBuffer() const { return m_visibleIds; }
        RHIBufferHandle Arguments() const { return m_arguments; }
        bool UsesHistory() const { return bool(m_historyPyramid); }
        const std::shared_ptr<const GpuGeometryOcclusion::Pyramid>& PreparedOcclusionPyramid() const { return m_preparedPyramid; }
        const std::shared_ptr<const material_graph::MeshSurfaceBatch>& MeshletGeometry(std::uint32_t bin) const { return m_meshletGeometries.at(bin); }
        std::uint32_t MeshletOutputOffset(std::uint32_t bin) const { return m_meshletOffsets.at(bin); }
        PreparedStats GetPreparedStats() const { return m_preparedStats; }
        std::uint64_t ArgsOffset(std::uint32_t bin) const;

      private:
        friend class GpuGeometryVisibility;
        Frame() = default;
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const;
        void Dispatch(RHIEncoder& encoder, bool reset, RHIBindingTable depth = {}) const;

        std::vector<std::shared_ptr<const material_graph::MeshSurfaceBatch>> m_meshletGeometries;
        std::vector<std::uint32_t> m_meshletOffsets;
        RHIBindingTable m_meshletVertices, m_meshletTopologies;
        std::uint32_t m_meshletGroups{};
        PreparedStats m_preparedStats{};
        IRenderDeviceServices* m_device{};
        std::uint64_t m_recording{}, m_descriptors{};
        std::uint32_t m_candidateCount{}, m_binCount{}, m_outputCount{};
        std::uint32_t m_width{}, m_height{}, m_occlusionLevelCount{};
        RHIBufferHandle m_visibleIds, m_arguments;
        std::shared_ptr<BufferPool> m_bufferPool;
        std::uint64_t m_visibleBytes{}, m_argumentBytes{};
        mutable std::atomic<bool> m_poolReusable{true};
        RHIBufferSlice m_candidates, m_bins, m_constants;
        RHIBindingTable m_outputs;
        RHIBufferSlice m_historyProjection;
        std::shared_ptr<const Frame> m_firstStage;
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> m_historyPyramid;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_reset, m_cull, m_meshletFinalize;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_occlusionCull;
        GpuGeometryOcclusion::ViewKey m_occlusionView;
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> m_preparedPyramid;
        mutable std::shared_ptr<const GpuGeometryOcclusion::Pyramid> m_pyramid;
        std::weak_ptr<const Frame> m_self;
        mutable const EnhancedRenderGraph* m_graph{};
        mutable std::uint64_t m_graphEpoch{};
        mutable RGHandle m_graphVisibleIds, m_graphArguments;
    };

    GpuGeometryVisibility() = default;
    ~GpuGeometryVisibility();
    GpuGeometryVisibility(const GpuGeometryVisibility&) = delete;
    GpuGeometryVisibility& operator=(const GpuGeometryVisibility&) = delete;

    // The matrix uses the engine's row-vector convention and zero-to-one clip Z.
    // All candidates of a bin share its aligned outputOffset. Bin output ranges
    // must not overlap; sourceIndex is an opaque caller-owned ID (an instance
    // index or a METEOR owner ID). Empty inputs and unsupported devices return
    // success with a null result; unsupported devices use direct fallback.
    // currentFrameOcclusion prepares the optional standard-Z second stage;
    // Declare alone still uses frustum-only visibility on compatibility graphs.
    // Preflight during the caller's backend shader-output scope. Ordered range
    // bins can then be allocated during declaration without compiling new PSOs.
    bool PreparePipelines(const EnhancedFrameContext& context, std::string& error);

    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                 std::span<const Candidate> candidates, std::span<const Bin> bins,
                 std::shared_ptr<const Frame>& result, std::string& error,
                 bool currentFrameOcclusion = false);
    // Optional-pipeline preflight before choosing any prepass/main geometry route.
    // False may disable the whole view's HZB without allocating candidate buffers.
    bool PrepareOcclusionPipelines(const EnhancedFrameContext& context, std::string& error);

    // Posed world-space vertices and exact camera/light projection. GPU writes
    // an aligned {survivorCount, meshletIDs...} list and bounded 2D arguments.
    bool PrepareMeshlets(const EnhancedFrameContext& context, const math::matrix4x4& projection,
        std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries,
        std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion = false,
        std::shared_ptr<const GpuGeometryOcclusion::Pyramid> history = {},
        std::shared_ptr<const Frame> firstStage = {});

    // Direct draws use the same vertex-ID indirection with an identity mapping.
    static RHIBufferSlice UploadIdentity(const EnhancedFrameContext& context, std::uint32_t count,
                                         std::string& error);
    void ShutdownAfterIdle();

  private:
    struct Recording
    {
        std::vector<std::shared_ptr<const Frame>> owners;
        std::uint64_t completion{};
        bool submitted{}, quarantined{};
    };

    bool PrepareInternal(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                         std::span<const Candidate> candidates, std::span<const Bin> bins,
                         std::shared_ptr<const Frame>& result, std::string& error,
                         bool currentFrameOcclusion,
                         std::span<const std::shared_ptr<const material_graph::MeshSurfaceBatch>> geometries = {});

    bool Initialize(const EnhancedFrameContext& context, std::string& error);
    bool InitializeMeshlets(const EnhancedFrameContext& context, std::string& error);
    bool InitializeMeshletOcclusion(const EnhancedFrameContext& context, std::string& error);
    bool InitializeOcclusion(const EnhancedFrameContext& context, std::string& error);
    void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recording) override;
    void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion) override;

    IRenderDeviceServices* m_device{};
    std::shared_ptr<BufferPool> m_bufferPool;
    LX::Runtime::ComputePipeline m_reset, m_cull, m_meshletReset, m_meshletCull, m_meshletFinalize;
    LX::Runtime::ComputePipeline m_meshletOcclusionCull;
    LX::Runtime::ComputePipeline m_meshletHistoryCull, m_meshletRecoveryCull;
    LX::Runtime::ComputePipeline m_occlusionCull;
    GpuGeometryOcclusion m_depthPyramid;
    std::mutex m_recordingMutex;
    std::map<std::uint64_t, Recording> m_recordings;
    std::uint64_t m_completed{};
};
