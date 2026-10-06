#pragma once

#include "LXMaterialPipeline.h"
#include "GpuGeometryOcclusion.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector4.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct EnhancedFrameContext;

// One material/geometry batch. Every candidate is {batch-local instance, meshlet}
// and every surviving pair becomes exactly one mesh-shader group. CPU chooses the
// PSO/bin; GPU owns the compacted work and indirect X count. No CPU count readback.
class GpuMeshletVisibility final : private IRHIUploadTransactionListener
{
public:
    static constexpr std::uint32_t kMaximumCandidates = 65535;
    static constexpr std::uint32_t kMaximumLods = 8;

    class Frame
    {
    public:
        ~Frame();
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        void Declare(EnhancedRenderGraph& graph) const;
        void DeclareWithOcclusion(EnhancedRenderGraph& graph,
            std::shared_ptr<const GpuGeometryOcclusion::Pyramid> pyramid) const;
        void AddReadUsages(EnhancedRenderGraph& graph,
            std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        // StructuredBuffer<uint2>: {batch-local instance, meshlet ordinal}.
        RHIBufferSlice VisibleIds() const;
        RHIBufferHandle Arguments() const { return m_arguments; }
        std::uint64_t ArgsOffset() const { return 0; }

    private:
        friend class GpuMeshletVisibility;
        Frame() = default;
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const;
        void Dispatch(RHIEncoder& encoder, bool reset, RHIBindingTable depth = {}) const;

        IRenderDeviceServices* m_device{};
        std::uint64_t m_recording{}, m_descriptors{};
        std::uint32_t m_candidateCount{};
        RHIBufferHandle m_visibleIds, m_arguments;
        RHIBufferSlice m_worlds, m_constants;
        RHIMeshletBinding m_meshlets;
        RHIBindingTable m_outputs;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_reset, m_cull;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_occlusionCull;
        GpuGeometryOcclusion::ViewKey m_occlusionView;
        mutable std::shared_ptr<const GpuGeometryOcclusion::Pyramid> m_pyramid;
        std::shared_ptr<const Frame> m_source;
        std::weak_ptr<const Frame> m_self;
        mutable const EnhancedRenderGraph* m_graph{};
        mutable std::uint64_t m_graphEpoch{};
        mutable RGHandle m_graphVisibleIds, m_graphArguments, m_graphMeshlets;
    };

    GpuMeshletVisibility() = default;
    ~GpuMeshletVisibility();
    GpuMeshletVisibility(const GpuMeshletVisibility&) = delete;
    GpuMeshletVisibility& operator=(const GpuMeshletVisibility&) = delete;

    // Call after the graph upload-prefix boundary. Mesh data must be a validated
    // immutable profile-1 cache binding in ShaderResource state; its cache owner
    // retains it through this recording's completion. Worlds are native row-vector
    // affine transforms matching the raster instance buffer. Static deformation
    // semantics and a compatible mesh PSO are the caller's responsibility.
    // Empty/unsupported/profile/over-limit input returns true with null result,
    // so callers choose indexed fallback before declaring/recording either route.
    // Shader/compiler/PSO prerequisite failure also returns true/null, retaining
    // an error diagnostic for the caller's fallback reporting. Buffer allocation
    // and recording failures return false and must not silently drop geometry.
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
        const RHIMeshletBinding& meshlets, std::span<const math::matrix4x4> worlds,
        std::shared_ptr<const Frame>& result, std::string& error, bool currentFrameOcclusion = false);
    // Bindings/errors include LOD0 at index zero (error zero). All levels share
    // the exact finalized vertices, static affine worlds, material and PSO.
    // localSphere encloses all base-indexed vertices; validated coarse indices
    // reference only that set. Outputs preserve level order;
    // the consumer must submit every output against that level's binding.
    // Preparation is atomic: any unavailable level yields an empty result so
    // the caller renders one unselected LOD0 fallback, never a partial chain.
    bool PrepareLods(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
        std::span<const RHIMeshletBinding> lodBindings, std::span<const float> geometricErrors,
        const math::vector4& localSphere, std::span<const math::matrix4x4> worlds,
        std::vector<std::shared_ptr<const Frame>>& result, std::string& error, float maxPixelError = 1.0f,
        bool currentFrameOcclusion = false);
    bool PrepareOcclusionPipelines(const EnhancedFrameContext& context, std::string& error);
    // Filters the actual first-pass GPU pairs/count, preserving its exact LOD
    // choice and geometry even across compiler variants. Requires a shared
    // pyramid at declaration. A null result keeps the original frustum frame.
    bool PrepareOcclusionRecheck(const EnhancedFrameContext& context, std::shared_ptr<const Frame> prior,
        std::shared_ptr<const Frame>& result, std::string& error);
    void ShutdownAfterIdle();

private:
    struct LodSelection
    {
        std::array<float, kMaximumLods> errors{};
        math::vector4 localSphere{};
        std::uint32_t count{1}, index{};
        float maxPixelError{1.0f};
    };
    struct Recording
    {
        std::vector<std::shared_ptr<const Frame>> owners;
        std::uint64_t completion{};
        bool submitted{}, quarantined{};
    };
    bool Initialize(const EnhancedFrameContext& context, std::string& error);
    bool InitializeOcclusion(const EnhancedFrameContext& context, std::string& error);
    bool PrepareLod(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
        const RHIMeshletBinding& meshlets, std::span<const math::matrix4x4> worlds,
        const LodSelection& lod, bool currentFrameOcclusion, std::shared_ptr<const Frame>& result, std::string& error);
    void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recording) override;
    void OnUploadSubmissionRejected(std::uint64_t recording, RHICompletionPoint completion) override;

    IRenderDeviceServices* m_device{};
    LX::Runtime::ComputePipeline m_reset, m_cull;
    LX::Runtime::ComputePipeline m_occlusionCull, m_occlusionRecheck;
    std::mutex m_recordingMutex;
    std::map<std::uint64_t, Recording> m_recordings;
    std::uint64_t m_completed{};
};
