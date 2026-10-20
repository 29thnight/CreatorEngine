#include "RHISoftwareEncoder.h"

void RHISoftwareEncoder::SetViewportAndScissor(uint32_t width, uint32_t height)
{
    RHISoftwareSetViewportAndScissorArguments payload{width, height};
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetViewportAndScissor, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetViewportAndScissorArguments*>(raw);
                                             encoder.SetViewportAndScissor(value.width, value.height);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

bool RHISoftwareEncoder::SetViewport(float x, float y, uint32_t width, uint32_t height)
{
    RHISoftwareSetViewportArguments payload{x, y, width, height};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::SetViewport, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetViewport, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetViewportArguments*>(raw);
                                             return encoder.SetViewport(value.x, value.y, value.width, value.height);
                                         });
    return accepted;
}

bool RHISoftwareEncoder::SetScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    RHISoftwareSetScissorArguments payload{x, y, width, height};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::SetScissor, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetScissor, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetScissorArguments*>(raw);
                                             return encoder.SetScissor(value.x, value.y, value.width, value.height);
                                         });
    return accepted;
}

void RHISoftwareEncoder::SetPipeline(RHIBindPoint bindPoint, RHIPipelineHandle pipeline)
{
    RHISoftwareSetPipelineArguments payload{bindPoint, pipeline};
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetPipeline, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetPipelineArguments*>(raw);
                                             encoder.SetPipeline(value.bindPoint, value.pipeline);
                                             return true;
                                         });
    if (accepted)
    {
        m_state.boundPipeline = pipeline;
        m_state.bindPoint = bindPoint;
    }
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetPrimitiveTopology(RHIPrimitiveTopology topology)
{
    RHISoftwareSetPrimitiveTopologyArguments payload{topology};
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetPrimitiveTopology, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetPrimitiveTopologyArguments*>(raw);
                                             encoder.SetPrimitiveTopology(value.topology);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetBindings(RHIBindPoint bindPoint, uint32_t slot, const RHIBindingTable& value)
{
    RHISoftwareSetBindingsArguments payload{bindPoint, slot, value};
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetBindings, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetBindingsArguments*>(raw);
                                             encoder.SetBindings(value.bindPoint, value.slot, value.value);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetSamplers(RHIBindPoint bindPoint, uint32_t slot, const RHISamplerTable& value)
{
    RHISoftwareSetSamplersArguments payload{bindPoint, slot, value};
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetSamplers, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetSamplersArguments*>(raw);
                                             encoder.SetSamplers(value.bindPoint, value.slot, value.value);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetConstantBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value)
{
    RHISoftwareSetConstantBufferArguments payload{bindPoint, slot, value};
    payload.value.cpuAddress = nullptr;
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetConstantBuffer, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetConstantBufferArguments*>(raw);
                                             encoder.SetConstantBuffer(value.bindPoint, value.slot, value.value);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetRootBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value)
{
    RHISoftwareSetRootBufferArguments payload{bindPoint, slot, value};
    payload.value.cpuAddress = nullptr;
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetRootBuffer, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetRootBufferArguments*>(raw);
                                             encoder.SetRootBuffer(value.bindPoint, value.slot, value.value);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

bool RHISoftwareEncoder::SetRootUnorderedBuffer(RHIBindPoint bindPoint, uint32_t slot, const RHIBufferSlice& value)
{
    RHISoftwareSetRootUnorderedBufferArguments payload{bindPoint, slot, value};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::SetRootUnorderedBuffer, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    payload.value.cpuAddress = nullptr;
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::SetRootUnorderedBuffer, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareSetRootUnorderedBufferArguments*>(raw);
                           return encoder.SetRootUnorderedBuffer(value.bindPoint, value.slot, value.value);
                       });
    return accepted;
}

void RHISoftwareEncoder::SetVertexBuffer(const RHIBufferSlice& slice, uint32_t stride)
{
    RHISoftwareSetVertexBufferArguments payload{slice, stride};
    payload.slice.cpuAddress = nullptr;
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetVertexBuffer, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetVertexBufferArguments*>(raw);
                                             encoder.SetVertexBuffer(value.slice, value.stride);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::SetIndexBuffer(const RHIBufferSlice& slice, RHIFormat format)
{
    RHISoftwareSetIndexBufferArguments payload{slice, format};
    payload.slice.cpuAddress = nullptr;
    const bool accepted = m_list->Append(RHISoftwareCommandType::SetIndexBuffer, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareSetIndexBufferArguments*>(raw);
                                             encoder.SetIndexBuffer(value.slice, value.format);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                              uint32_t firstInstance)
{
    RHISoftwareDrawArguments payload{vertexCount, instanceCount, firstVertex, firstInstance};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::Draw, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareDrawArguments*>(raw);
                           encoder.Draw(value.vertexCount, value.instanceCount, value.firstVertex, value.firstInstance);
                           return true;
                       });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                     int32_t baseVertex, uint32_t firstInstance)
{
    RHISoftwareDrawIndexedArguments payload{indexCount, instanceCount, firstIndex, baseVertex, firstInstance};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::DrawIndexed, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareDrawIndexedArguments*>(raw);
                           encoder.DrawIndexed(value.indexCount, value.instanceCount, value.firstIndex,
                                               value.baseVertex, value.firstInstance);
                           return true;
                       });
    static_cast<void>(accepted);
}

bool RHISoftwareEncoder::DrawIndirect(RHIBufferHandle arguments, uint64_t byteOffset)
{
    RHISoftwareDrawIndirectArguments payload{arguments, byteOffset};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DrawIndirect, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::DrawIndirect, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareDrawIndirectArguments*>(raw);
                                             return encoder.DrawIndirect(value.arguments, value.byteOffset);
                                         });
    return accepted;
}

bool RHISoftwareEncoder::DrawIndexedIndirect(RHIBufferHandle arguments, uint64_t byteOffset)
{
    RHISoftwareDrawIndexedIndirectArguments payload{arguments, byteOffset};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DrawIndexedIndirect, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::DrawIndexedIndirect, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareDrawIndexedIndirectArguments*>(raw);
                                             return encoder.DrawIndexedIndirect(value.arguments, value.byteOffset);
                                         });
    return accepted;
}

bool RHISoftwareEncoder::DispatchIndirect(RHIBufferHandle arguments, uint64_t byteOffset)
{
    RHISoftwareDispatchIndirectArguments payload{arguments, byteOffset};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DispatchIndirect, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::DispatchIndirect, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareDispatchIndirectArguments*>(raw);
                                             return encoder.DispatchIndirect(value.arguments, value.byteOffset);
                                         });
    if (accepted)
    {
        ++m_list->m_stats.computeDispatchCommands;
    }
    return accepted;
}

bool RHISoftwareEncoder::DispatchMeshIndirect(RHIBufferHandle arguments, uint64_t byteOffset)
{
    RHISoftwareDispatchMeshIndirectArguments payload{arguments, byteOffset};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DispatchMeshIndirect, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::DispatchMeshIndirect, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareDispatchMeshIndirectArguments*>(raw);
                                             return encoder.DispatchMeshIndirect(value.arguments, value.byteOffset);
                                         });
    return accepted;
}

bool RHISoftwareEncoder::DispatchMeshIndirectBatch(RHIBufferHandle arguments, uint64_t byteOffset, uint32_t count,
                                                   std::span<const uint32_t> roots)
{
    RHISoftwareDispatchMeshIndirectBatchArguments payload{arguments, byteOffset, count, roots};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DispatchMeshIndirectBatch, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    payload.roots = m_list->CopySpan(roots);
    const bool accepted = m_list->Append(
        RHISoftwareCommandType::DispatchMeshIndirectBatch, payload,
        [](RHIEncoder& encoder, const void* raw)
        {
            const auto& value = *static_cast<const RHISoftwareDispatchMeshIndirectBatchArguments*>(raw);
            return encoder.DispatchMeshIndirectBatch(value.arguments, value.byteOffset, value.count, value.roots);
        });
    return accepted;
}

bool RHISoftwareEncoder::DrawIndexedIndirectBatch(RHIBufferHandle arguments, uint64_t byteOffset, uint32_t count,
                                                  std::span<const uint32_t> roots)
{
    RHISoftwareDrawIndexedIndirectBatchArguments payload{arguments, byteOffset, count, roots};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DrawIndexedIndirectBatch, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    payload.roots = m_list->CopySpan(roots);
    const bool accepted = m_list->Append(
        RHISoftwareCommandType::DrawIndexedIndirectBatch, payload,
        [](RHIEncoder& encoder, const void* raw)
        {
            const auto& value = *static_cast<const RHISoftwareDrawIndexedIndirectBatchArguments*>(raw);
            return encoder.DrawIndexedIndirectBatch(value.arguments, value.byteOffset, value.count, value.roots);
        });
    return accepted;
}

void RHISoftwareEncoder::Dispatch(uint32_t x, uint32_t y, uint32_t z)
{
    RHISoftwareDispatchArguments payload{x, y, z};
    const bool accepted = m_list->Append(RHISoftwareCommandType::Dispatch, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value = *static_cast<const RHISoftwareDispatchArguments*>(raw);
                                             encoder.Dispatch(value.x, value.y, value.z);
                                             return true;
                                         });
    if (accepted)
    {
        ++m_list->m_stats.computeDispatchCommands;
    }
    static_cast<void>(accepted);
}

bool RHISoftwareEncoder::DispatchMesh(uint32_t x, uint32_t y, uint32_t z)
{
    RHISoftwareDispatchMeshArguments payload{x, y, z};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::DispatchMesh, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted = m_list->Append(RHISoftwareCommandType::DispatchMesh, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareDispatchMeshArguments*>(raw);
                                             return encoder.DispatchMesh(value.x, value.y, value.z);
                                         });
    return accepted;
}

bool RHISoftwareEncoder::BeginRasterWithoutAttachments(uint32_t width, uint32_t height)
{
    RHISoftwareBeginRasterWithoutAttachmentsArguments payload{width, height};
    if (!m_list->CanWrite() || !m_validator->Validate(RHISoftwareCommandType::BeginRasterWithoutAttachments, &payload,
                                                      sizeof(payload), m_state))
    {
        return false;
    }
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::BeginRasterWithoutAttachments, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value =
                               *static_cast<const RHISoftwareBeginRasterWithoutAttachmentsArguments*>(raw);
                           return encoder.BeginRasterWithoutAttachments(value.width, value.height);
                       });
    return accepted;
}

void RHISoftwareEncoder::BindRenderTargets(const RHIRenderTargetBinding& binding)
{
    RHISoftwareBindRenderTargetsArguments payload{binding};
    const bool accepted = m_list->Append(RHISoftwareCommandType::BindRenderTargets, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareBindRenderTargetsArguments*>(raw);
                                             encoder.BindRenderTargets(value.binding);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::ClearRenderTargets(const RHIRenderTargetBinding& binding, const float* rgba)
{
    if (!rgba)
    {
        m_list->Fail("ClearRenderTargets requires a four-float color.");
        return;
    }
    RHISoftwareClearRenderTargetsArguments payload{binding, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    const bool accepted = m_list->Append(RHISoftwareCommandType::ClearRenderTargets, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareClearRenderTargetsArguments*>(raw);
                                             encoder.ClearRenderTargets(value.binding, value.rgba.data());
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::ClearDepthTarget(const RHIRenderTargetBinding& binding, float depth)
{
    RHISoftwareClearDepthTargetArguments payload{binding, depth};
    const bool accepted = m_list->Append(RHISoftwareCommandType::ClearDepthTarget, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareClearDepthTargetArguments*>(raw);
                                             encoder.ClearDepthTarget(value.binding, value.depth);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::ResourceBarriers(const RHIBarrierBatch& barriers)
{
    RHISoftwareResourceBarriersArguments payload{barriers};
    payload.barriers.textureTransitions = m_list->CopySpan(barriers.textureTransitions);
    payload.barriers.bufferTransitions = m_list->CopySpan(barriers.bufferTransitions);
    payload.barriers.uavTextures = m_list->CopySpan(barriers.uavTextures);
    payload.barriers.uavBuffers = m_list->CopySpan(barriers.uavBuffers);
    payload.barriers.aliasTextures = m_list->CopySpan(barriers.aliasTextures);
    payload.barriers.aliasBuffers = m_list->CopySpan(barriers.aliasBuffers);
    const bool accepted = m_list->Append(RHISoftwareCommandType::ResourceBarriers, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareResourceBarriersArguments*>(raw);
                                             encoder.ResourceBarriers(value.barriers);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::UavBarrier(std::span<const RHITextureHandle> textures)
{
    RHISoftwareUavBarrierArguments payload{textures};
    payload.textures = m_list->CopySpan(textures);
    const bool accepted = m_list->Append(RHISoftwareCommandType::UavBarrier, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareUavBarrierArguments*>(raw);
                                             encoder.UavBarrier(value.textures);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::UavBarrierBuffers(std::span<const RHIBufferHandle> buffers)
{
    RHISoftwareUavBarrierBuffersArguments payload{buffers};
    payload.buffers = m_list->CopySpan(buffers);
    const bool accepted = m_list->Append(RHISoftwareCommandType::UavBarrierBuffers, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareUavBarrierBuffersArguments*>(raw);
                                             encoder.UavBarrierBuffers(value.buffers);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::CopyResource(RHITextureHandle destination, RHITextureHandle source)
{
    RHISoftwareCopyResourceArguments payload{destination, source};
    const bool accepted = m_list->Append(RHISoftwareCommandType::CopyResource, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareCopyResourceArguments*>(raw);
                                             encoder.CopyResource(value.destination, value.source);
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::ClearUnorderedAccess(const RHIBindingDesc& view, const float* rgba)
{
    if (!rgba)
    {
        m_list->Fail("ClearUnorderedAccess requires a four-float color.");
        return;
    }
    RHISoftwareClearUnorderedAccessArguments payload{view, {rgba[0], rgba[1], rgba[2], rgba[3]}};
    const bool accepted = m_list->Append(RHISoftwareCommandType::ClearUnorderedAccess, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareClearUnorderedAccessArguments*>(raw);
                                             encoder.ClearUnorderedAccess(value.view, value.rgba.data());
                                             return true;
                                         });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::CopyToReadback(const RHIReadback& readback, RHITextureHandle source, uint32_t slice,
                                        uint32_t sourceSubresource)
{
    RHISoftwareCopyToReadbackArguments payload{readback, source, slice, sourceSubresource};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::CopyToReadback, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareCopyToReadbackArguments*>(raw);
                           encoder.CopyToReadback(value.readback, value.source, value.slice, value.sourceSubresource);
                           return true;
                       });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::CopyPartialToReadback(const RHIReadback& readback, RHITextureHandle source, uint32_t slice,
                                               uint32_t sourceSubresource)
{
    RHISoftwareCopyPartialToReadbackArguments payload{readback, source, slice, sourceSubresource};
    const bool accepted = m_list->Append(
        RHISoftwareCommandType::CopyPartialToReadback, payload,
        [](RHIEncoder& encoder, const void* raw)
        {
            const auto& value = *static_cast<const RHISoftwareCopyPartialToReadbackArguments*>(raw);
            encoder.CopyPartialToReadback(value.readback, value.source, value.slice, value.sourceSubresource);
            return true;
        });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::CopyVolumeToReadback(const RHIReadback& readback, RHITextureHandle source,
                                              uint32_t sourceSubresource)
{
    RHISoftwareCopyVolumeToReadbackArguments payload{readback, source, sourceSubresource};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::CopyVolumeToReadback, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareCopyVolumeToReadbackArguments*>(raw);
                           encoder.CopyVolumeToReadback(value.readback, value.source, value.sourceSubresource);
                           return true;
                       });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::CopyBufferToReadback(const RHIReadback& readback, RHIBufferHandle source,
                                              uint64_t sourceOffset, uint64_t bytes)
{
    RHISoftwareCopyBufferToReadbackArguments payload{readback, source, sourceOffset, bytes};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::CopyBufferToReadback, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareCopyBufferToReadbackArguments*>(raw);
                           encoder.CopyBufferToReadback(value.readback, value.source, value.sourceOffset, value.bytes);
                           return true;
                       });
    static_cast<void>(accepted);
}

bool RHISoftwareEncoder::CopyBuffer(const RHIBufferSlice& destination, const RHIBufferSlice& source)
{
    RHISoftwareCopyBufferArguments payload{destination, source};
    if (!m_list->CanWrite() ||
        !m_validator->Validate(RHISoftwareCommandType::CopyBuffer, &payload, sizeof(payload), m_state))
    {
        return false;
    }
    payload.destination.cpuAddress = nullptr;
    payload.source.cpuAddress = nullptr;
    const bool accepted = m_list->Append(RHISoftwareCommandType::CopyBuffer, payload,
                                         [](RHIEncoder& encoder, const void* raw)
                                         {
                                             const auto& value =
                                                 *static_cast<const RHISoftwareCopyBufferArguments*>(raw);
                                             return encoder.CopyBuffer(value.destination, value.source);
                                         });
    return accepted;
}

void RHISoftwareEncoder::CopyTexture(RHITextureHandle destination, RHITextureHandle source,
                                     uint32_t destinationSubresource, uint32_t sourceSubresource)
{
    RHISoftwareCopyTextureArguments payload{destination, source, destinationSubresource, sourceSubresource};
    const bool accepted = m_list->Append(
        RHISoftwareCommandType::CopyTexture, payload,
        [](RHIEncoder& encoder, const void* raw)
        {
            const auto& value = *static_cast<const RHISoftwareCopyTextureArguments*>(raw);
            encoder.CopyTexture(value.destination, value.source, value.destinationSubresource, value.sourceSubresource);
            return true;
        });
    static_cast<void>(accepted);
}

void RHISoftwareEncoder::ClearRenderTargetRect(const RHIRenderTargetBinding& binding, const float* rgba,
                                               const RHIRect& rect)
{
    if (!rgba)
    {
        m_list->Fail("ClearRenderTargetRect requires a four-float color.");
        return;
    }
    RHISoftwareClearRenderTargetRectArguments payload{binding, {rgba[0], rgba[1], rgba[2], rgba[3]}, rect};
    const bool accepted =
        m_list->Append(RHISoftwareCommandType::ClearRenderTargetRect, payload,
                       [](RHIEncoder& encoder, const void* raw)
                       {
                           const auto& value = *static_cast<const RHISoftwareClearRenderTargetRectArguments*>(raw);
                           encoder.ClearRenderTargetRect(value.binding, value.rgba.data(), value.rect);
                           return true;
                       });
    static_cast<void>(accepted);
}
