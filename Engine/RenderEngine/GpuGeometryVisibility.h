#pragma once

#include "LXMaterialPipeline.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector4.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

struct EnhancedFrameContext;

// A recording owns its candidates, compacted IDs and indirect arguments. Buffers
// are never overwritten by another view/frame or released while submitted work
// can still read them. Call ShutdownAfterIdle before destroying the device.
class GpuGeometryVisibility final : private IRHIUploadTransactionListener
{
  public:
    static constexpr std::uint32_t kConservative = 1;
    // Root storage-buffer offsets must satisfy both backends' alignment limits.
    // Units are uint32 IDs, so each bin starts at a 256-byte boundary.
    static constexpr std::uint32_t kOutputAlignment = 64;

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
        std::uint32_t padding{};
    };
    static_assert(sizeof(Bin) == 16);

    class Frame
    {
      public:
        ~Frame();
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;

        void Declare(EnhancedRenderGraph& graph) const;
        void AddReadUsages(EnhancedRenderGraph& graph,
                           std::vector<EnhancedRenderGraph::RGPassUsage>& usages) const;
        RHIBufferSlice VisibleIds(std::uint32_t outputOffset, std::uint32_t count) const;
        RHIBufferHandle Arguments() const { return m_arguments; }
        std::uint64_t ArgsOffset(std::uint32_t bin) const;

      private:
        friend class GpuGeometryVisibility;
        Frame() = default;
        void CheckCurrent(const EnhancedRenderGraph* graph = nullptr) const;
        void Dispatch(RHIEncoder& encoder, bool reset) const;

        IRenderDeviceServices* m_device{};
        std::uint64_t m_recording{}, m_descriptors{};
        std::uint32_t m_candidateCount{}, m_binCount{}, m_outputCount{};
        RHIBufferHandle m_visibleIds, m_arguments;
        RHIBufferSlice m_candidates, m_bins, m_constants;
        RHIBindingTable m_outputs;
        std::shared_ptr<const LX::Runtime::ComputeGeneration> m_reset, m_cull;
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
    // index or an LX owner ID). Empty inputs and unsupported devices return
    // success with a null result; unsupported devices use direct fallback.
    bool Prepare(const EnhancedFrameContext& context, const math::matrix4x4& viewProjection,
                 std::span<const Candidate> candidates, std::span<const Bin> bins,
                 std::shared_ptr<const Frame>& result, std::string& error);

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

    bool Initialize(const EnhancedFrameContext& context, std::string& error);
    void OnUploadSubmitted(std::uint64_t recording, RHICompletionPoint completion) override;
    void OnUploadCompleted(std::uint64_t completed) override;
    void OnUploadAborted(std::uint64_t recording) override;

    IRenderDeviceServices* m_device{};
    LX::Runtime::ComputePipeline m_reset, m_cull;
    std::mutex m_recordingMutex;
    std::map<std::uint64_t, Recording> m_recordings;
    std::uint64_t m_completed{};
};
