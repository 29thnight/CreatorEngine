#pragma once

#include "MaterialGraphMeshSurface.h"
#include "Render/Graph/EnhancedRenderGraph.h"

#include <atomic>

namespace material_graph
{
// Bounded opaque capture. Shared-depth capture reads a complete opaque draw set;
// private-depth capture owns depth for one material's mesh chunks.
struct RasterSurfaceRequest
{
    std::uint32_t width{}, height{};
    math::matrix4x4 viewProjection;
    SurfaceTextureFootprint texture;
};

class RasterSurfaceBatch final : public SurfaceGeometrySource
{
  public:
    ~RasterSurfaceBatch() override;
    RHIBufferHandle Buffer() const override { return buffer_; }
    std::uint32_t Count() const override { return request_.width * request_.height; }
    const IRenderDeviceServices* Device() const override { return device_; }
    const SurfaceView& View() const override { return meshes_.front()->View(); }
    std::uint64_t RecordingId() const override { return recordingId_; }
    bool IsValidated() const override { return validated_; }
    bool IsReadyForEvaluation() const override
    {
        return recordedStages_.load() == 7 && (!depthSource_ || depthSource_->IsReadyForEvaluation());
    }
    bool IsPreparedForGraph() const override { return IsCurrent(); }
    RGHandle GraphOutput(const EnhancedRenderGraph& graph) const override;
    bool IsCovered(std::uint32_t index) const override;
    bool ValidateReadback(std::span<const SurfacePoint> points, std::string& error) const override;
    const RasterSurfaceRequest& Request() const { return request_; }
    // Diagnostic footprint only. Leaves ShaderResource after graph execution.
    RHITextureHandle Derivatives() const { return targets_[5]; }
    RHITextureHandle Depth() const { return depth_; }
    // Single declaration in the same recording/descriptor generation as Prepare.
    // Graph callbacks retain this owner; the host retains it until GPU completion.
    // Sequential or parallel recording. Check IsReadyForEvaluation after record;
    // successful recording still requires GPU completion/readback acceptance.
    bool Declare(EnhancedRenderGraph& graph, std::string& error) const;

  private:
    friend class RasterSurfaceCollector;
    RasterSurfaceBatch() = default;
    bool IsCurrent() const;
    IRenderDeviceServices* device_{};
    RasterSurfaceRequest request_;
    std::vector<std::shared_ptr<const MeshSurfaceBatch>> meshes_;
    std::shared_ptr<const RasterSurfaceBatch> depthSource_;
    std::vector<RHIBufferSlice> indices_;
    std::array<RHITextureHandle, 6> targets_;
    RHITextureHandle depth_;
    RHIBufferHandle buffer_;
    RHIRenderTargetBinding renderTargets_;
    RHIBindingTable inputs_, output_, opaqueOwners_;
    RHIBufferSlice constants_;
    std::vector<RHIBufferSlice> drawConstants_;
    RHIPipelineHandle capturePipeline_, resolvePipeline_;
    bool doubleSided_{};
    std::uint64_t recordingId_{}, descriptorVersion_{};
    mutable std::atomic<unsigned> recordedStages_{};
    mutable std::atomic<bool> declared_{};
    mutable const EnhancedRenderGraph* graph_{};
    mutable std::uint64_t graphEpoch_{};
    mutable RGHandle graphOutput_;
    mutable std::vector<std::uint8_t> coverage_;
    mutable bool validated_{};
    std::weak_ptr<const RasterSurfaceBatch> self_;
};

class RasterSurfaceCollector
{
  public:
    static constexpr std::uint32_t MaxPixels = IblBaker::MaxPoints;
    static constexpr std::uint32_t MaxDraws = 64;
    // Runtime consumes compiled host artifacts; initialization is candidate-first.
    bool Initialize(IRenderDeviceServices& device, IRenderRootSignatureCache& roots, IRenderPipelineCache& pipelines,
                    const RHIShaderBlob& vertex, const RHIShaderBlob& pixel, const RHIShaderBlob& resolve,
                    const RHIShaderBlob& sharedPixel, bool doubleSided, std::string& error);
    // Resource/descriptor/upload allocation happens here, before Declare/Record.
    // Inputs must be unsampled world-frame buffers. All draws share an exact view.
    // Failed preparation leaves the caller's previous batch intact.
    bool Prepare(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                 std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                 std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error);
    // The complete opaque depth source is declared first in the same graph.
    // Each material reads that D32 depth and exact draw winner, without clear/write.
    // All mesh producers and the depth source must pass completion acceptance.
    bool PrepareSharedDepth(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                            std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                            std::shared_ptr<const RasterSurfaceBatch> depthSource,
                            std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error);

  private:
    bool PrepareImpl(IRenderDeviceServices& device, const RasterSurfaceRequest& request,
                     std::span<const std::shared_ptr<const MeshSurfaceBatch>> meshes,
                     std::shared_ptr<const RasterSurfaceBatch> depthSource,
                     std::shared_ptr<const RasterSurfaceBatch>& result, std::string& error);
    IRenderDeviceServices* device_{};
    RHIPipelineHandle capturePipeline_, sharedDepthPipeline_, resolvePipeline_;
    bool doubleSided_{};
};
} // namespace material_graph
