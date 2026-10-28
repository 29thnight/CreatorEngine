#include "../../Containers/ContainerAPI.h"
#pragma once
#include "RHISoftwareCommandList.h"

// Value payloads are shared by record-time device validation and replay.
struct RHISoftwareSetViewportAndScissorArguments
{
    uint32_t width;
    uint32_t height;
};

struct RHISoftwareSetViewportArguments
{
    float x;
    float y;
    uint32_t width;
    uint32_t height;
};

struct RHISoftwareSetScissorArguments
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
};

struct RHISoftwareSetPipelineArguments
{
    RHIBindPoint bindPoint;
    RHIPipelineHandle pipeline;
};

struct RHISoftwareSetPrimitiveTopologyArguments
{
    RHIPrimitiveTopology topology;
};

struct RHISoftwareSetBindingsArguments
{
    RHIBindPoint bindPoint;
    uint32_t slot;
    RHIBindingTable value;
};

struct RHISoftwareSetSamplersArguments
{
    RHIBindPoint bindPoint;
    uint32_t slot;
    RHISamplerTable value;
};

struct RHISoftwareSetConstantBufferArguments
{
    RHIBindPoint bindPoint;
    uint32_t slot;
    RHIBufferSlice value;
};

struct RHISoftwareSetRootBufferArguments
{
    RHIBindPoint bindPoint;
    uint32_t slot;
    RHIBufferSlice value;
};

struct RHISoftwareSetRootUnorderedBufferArguments
{
    RHIBindPoint bindPoint;
    uint32_t slot;
    RHIBufferSlice value;
};

struct RHISoftwareSetVertexBufferArguments
{
    RHIBufferSlice slice;
    uint32_t stride;
};

struct RHISoftwareSetIndexBufferArguments
{
    RHIBufferSlice slice;
    RHIFormat format;
};

struct RHISoftwareDrawArguments
{
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};

struct RHISoftwareDrawIndexedArguments
{
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t baseVertex;
    uint32_t firstInstance;
};

struct RHISoftwareDrawIndirectArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
};

struct RHISoftwareDrawIndexedIndirectArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
};

struct RHISoftwareDispatchIndirectArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
};

struct RHISoftwareDispatchMeshIndirectArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
};

struct RHISoftwareDispatchMeshIndirectBatchArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
    uint32_t count;
    std::span<const uint32_t> roots;
};

struct RHISoftwareDrawIndexedIndirectBatchArguments
{
    RHIBufferHandle arguments;
    uint64_t byteOffset;
    uint32_t count;
    std::span<const uint32_t> roots;
};

struct RHISoftwareDispatchArguments
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct RHISoftwareDispatchMeshArguments
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

struct RHISoftwareBeginRasterWithoutAttachmentsArguments
{
    uint32_t width;
    uint32_t height;
};

struct RHISoftwareBindRenderTargetsArguments
{
    RHIRenderTargetBinding binding;
};

struct RHISoftwareClearRenderTargetsArguments
{
    RHIRenderTargetBinding binding;
    ce::fixed_array<float, 4> rgba;
};

struct RHISoftwareClearDepthTargetArguments
{
    RHIRenderTargetBinding binding;
    float depth;
};

struct RHISoftwareResourceBarriersArguments
{
    RHIBarrierBatch barriers;
};

struct RHISoftwareUavBarrierArguments
{
    std::span<const RHITextureHandle> textures;
};

struct RHISoftwareUavBarrierBuffersArguments
{
    std::span<const RHIBufferHandle> buffers;
};

struct RHISoftwareCopyResourceArguments
{
    RHITextureHandle destination;
    RHITextureHandle source;
};

struct RHISoftwareClearUnorderedAccessArguments
{
    RHIBindingDesc view;
    ce::fixed_array<float, 4> rgba;
};

struct RHISoftwareCopyToReadbackArguments
{
    RHIReadback readback;
    RHITextureHandle source;
    uint32_t slice;
    uint32_t sourceSubresource;
};

struct RHISoftwareCopyPartialToReadbackArguments
{
    RHIReadback readback;
    RHITextureHandle source;
    uint32_t slice;
    uint32_t sourceSubresource;
};

struct RHISoftwareCopyVolumeToReadbackArguments
{
    RHIReadback readback;
    RHITextureHandle source;
    uint32_t sourceSubresource;
};

struct RHISoftwareCopyBufferToReadbackArguments
{
    RHIReadback readback;
    RHIBufferHandle source;
    uint64_t sourceOffset;
    uint64_t bytes;
};

struct RHISoftwareCopyBufferArguments
{
    RHIBufferSlice destination;
    RHIBufferSlice source;
};

struct RHISoftwareCopyTextureArguments
{
    RHITextureHandle destination;
    RHITextureHandle source;
    uint32_t destinationSubresource;
    uint32_t sourceSubresource;
};

struct RHISoftwareClearRenderTargetRectArguments
{
    RHIRenderTargetBinding binding;
    ce::fixed_array<float, 4> rgba;
    RHIRect rect;
};

class RHISoftwareEncoder final : public RHIEncoder
{
  public:
    RHISoftwareEncoder(RHISoftwareCommandList& list, const IRHISoftwareCommandValidator& validator)
        : m_list(&list), m_validator(&validator)
    {
    }

    void Reset(RHISoftwareCommandList& list)
    {
        m_list = &list;
        m_state = {};
    }

    void Reset(RHISoftwareCommandList& list, const IRHISoftwareCommandValidator& validator)
    {
        m_validator = &validator;
        Reset(list);
    }
    void ReleaseRecording() noexcept
    {
        m_list = nullptr;
        m_validator = nullptr;
        m_state = {};
    }

    // Backend profiler adapters reserve query identity on the recording thread.
    // Their typed immutable payload emits only that query during replay.
    template<typename Payload>
    bool RecordGpuQuery(RHISoftwareCommandType type, const Payload& payload, bool (*replay)(RHIEncoder&, const void*))
    {
        if (type != RHISoftwareCommandType::BeginGpuPass && type != RHISoftwareCommandType::EndGpuPass)
        {
            return false;
        }
        return m_list->Append(type, payload, replay);
    }

    void SetViewportAndScissor(uint32_t width, uint32_t height) override;
    bool SetViewport(float x, float y, uint32_t width, uint32_t height) override;
    bool SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
    void SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline) override;
    void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;
    void SetBindings(RHIBindPoint bindPoint, uint32_t slot, const RHIBindingTable& value) override;
    void SetSamplers(RHIBindPoint bindPoint, uint32_t slot, const RHISamplerTable& value) override;
    void SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value) override;
    void SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value) override;
    bool SetRootUnorderedBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value) override;
    void SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride) override;
    void SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format) override;
    void Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex,
                     uint32_t firstInstance) override;
    bool DrawIndirect(RHIBufferHandle arguments, uint64_t byteOffset) override;
    bool DrawIndexedIndirect(RHIBufferHandle arguments, uint64_t byteOffset) override;
    bool DispatchIndirect(RHIBufferHandle arguments, uint64_t byteOffset) override;
    bool DispatchMeshIndirect(RHIBufferHandle arguments, uint64_t byteOffset) override;
    bool DispatchMeshIndirectBatch(RHIBufferHandle arguments, uint64_t byteOffset, uint32_t count,
                                   std::span<const uint32_t> roots) override;
    bool DrawIndexedIndirectBatch(RHIBufferHandle arguments, uint64_t byteOffset, uint32_t count,
                                  std::span<const uint32_t> roots) override;
    void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;
    bool DispatchMesh(uint32_t x, uint32_t y, uint32_t z) override;
    bool BeginRasterWithoutAttachments(uint32_t width, uint32_t height) override;
    void BindRenderTargets(const RHIRenderTargetBinding& binding) override;
    void ClearRenderTargets(const RHIRenderTargetBinding& binding, const float* rgba) override;
    void ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth) override;
    void ResourceBarriers(const RHIBarrierBatch& barriers) override;
    void UavBarrier(std::span<const RHITextureHandle> textures) override;
    void UavBarrierBuffers(std::span<const RHIBufferHandle> buffers) override;
    void CopyResource(RHITextureHandle destination, RHITextureHandle source) override;
    void ClearUnorderedAccess(const RHIBindingDesc& view, const float* rgba) override;
    void CopyToReadback(const RHIReadback& readback, RHITextureHandle source, uint32_t slice,
                        uint32_t sourceSubresource) override;
    void CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source, uint32_t slice,
                               uint32_t sourceSubresource) override;
    void CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
                              uint32_t sourceSubresource) override;
    void CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source, uint64_t sourceOffset,
                              uint64_t bytes) override;
    bool CopyBuffer(const RHIBufferSlice& destination, const RHIBufferSlice& source) override;
    void CopyTexture(RHITextureHandle destination, RHITextureHandle source, uint32_t destinationSubresource,
                     uint32_t sourceSubresource) override;
    void ClearRenderTargetRect(const RHIRenderTargetBinding& binding, const float* rgba, const RHIRect& rect) override;
    uint64_t GetComputeDispatchCount() const override { return UINT64_MAX; }

  private:
    RHISoftwareCommandList* m_list;
    const IRHISoftwareCommandValidator* m_validator;
    RHISoftwareEncoderState m_state;
};
