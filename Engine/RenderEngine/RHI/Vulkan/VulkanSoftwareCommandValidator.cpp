#include "VulkanSoftwareCommandValidator.h"
#include "VulkanDeviceResources.h"
#include "VulkanPipelineCache.h"

bool VulkanSoftwareCommandValidator::Validate(RHISoftwareCommandType type, const void* arguments, size_t bytes,
                                              const RHISoftwareEncoderState& state) const
{
    const auto bound = [&](RHIBindPoint point, bool mesh = false)
    {
        const auto pipeline = m_resources.m_pipelineCache
                                  ? m_resources.m_pipelineCache->Resolve(state.pipelines[static_cast<size_t>(point)])
                                  : VulkanPipelineEntry{};
        const auto kind = point == RHIBindPoint::Compute ? VulkanPipelineKind::Compute
                          : mesh                         ? VulkanPipelineKind::Mesh
                                                         : VulkanPipelineKind::Graphics;
        return pipeline.IsValid() && pipeline.kind == kind;
    };
    const auto indirect = [&](RHIBufferHandle handle, uint64_t offset, uint64_t required)
    {
        const auto entry = m_resources.m_resourceTable.Resolve(handle);
        return offset % 4 == 0 && entry.IsValid() && entry.allowIndirectArguments && offset <= entry.bytes &&
               required <= entry.bytes - offset;
    };
    switch (type)
    {
    case RHISoftwareCommandType::SetViewport:
    {
        const auto* value = Read<RHISoftwareSetViewportArguments>(arguments, bytes);
        if (!value || !std::isfinite(value->x) || !std::isfinite(value->y) || !value->width || !value->height)
        {
            return false;
        }
        const auto limits = m_resources.m_viewportLimits;
        const double right = double(value->x) + value->width;
        const double bottom = double(value->y) + value->height;
        if (value->width > limits.maxWidth || value->height > limits.maxHeight || value->x < limits.lowerBound ||
            value->y < limits.lowerBound || right > limits.upperBound || bottom > limits.upperBound)
        {
            return false;
        }
        const float nativeY = value->y + static_cast<float>(value->height);
        const float nativeWidth = static_cast<float>(value->width);
        const float nativeHeight = -static_cast<float>(value->height);
        const double nativeTop = double(nativeY) + nativeHeight;
        return std::isfinite(nativeY) && double(nativeWidth) <= limits.maxWidth &&
               -double(nativeHeight) <= limits.maxHeight && double(value->x) + nativeWidth <= limits.upperBound &&
               nativeY >= limits.lowerBound && nativeY <= limits.upperBound && nativeTop >= limits.lowerBound &&
               nativeTop <= limits.upperBound;
    }
    case RHISoftwareCommandType::SetScissor:
    {
        const auto* value = Read<RHISoftwareSetScissorArguments>(arguments, bytes);
        return value && value->x <= INT32_MAX && value->y <= INT32_MAX && value->width <= INT32_MAX - value->x &&
               value->height <= INT32_MAX - value->y;
    }
    case RHISoftwareCommandType::SetRootUnorderedBuffer:
    {
        const auto* value = Read<RHISoftwareSetRootUnorderedBufferArguments>(arguments, bytes);
        if (!value)
        {
            return false;
        }
        const auto entry = m_resources.m_resourceTable.Resolve(value->value.buffer);
        return entry.IsValid() && value->value.offset < entry.bytes &&
               (!value->value.size || value->value.size <= entry.bytes - value->value.offset);
    }
    case RHISoftwareCommandType::DrawIndirect:
    {
        const auto* value = Read<RHISoftwareDrawIndirectArguments>(arguments, bytes);
        return value && state.renderingOpen && bound(RHIBindPoint::Graphics) &&
               m_resources.GetIndirectDrawCapabilities().nonIndexedDraw && VulkanApi::vkCmdDrawIndirect &&
               indirect(value->arguments, value->byteOffset, sizeof(RHIDrawIndirectArguments));
    }
    case RHISoftwareCommandType::DrawIndexedIndirect:
    {
        const auto* value = Read<RHISoftwareDrawIndexedIndirectArguments>(arguments, bytes);
        return value && state.renderingOpen && bound(RHIBindPoint::Graphics) &&
               m_resources.GetIndirectDrawCapabilities().indexedDraw && VulkanApi::vkCmdDrawIndexedIndirect &&
               indirect(value->arguments, value->byteOffset, sizeof(RHIDrawIndexedIndirectArguments));
    }
    case RHISoftwareCommandType::DispatchIndirect:
    {
        const auto* value = Read<RHISoftwareDispatchIndirectArguments>(arguments, bytes);
        return value && !state.renderingOpen && bound(RHIBindPoint::Compute) && VulkanApi::vkCmdDispatchIndirect &&
               indirect(value->arguments, value->byteOffset, sizeof(RHIDispatchIndirectArguments));
    }
    case RHISoftwareCommandType::DispatchMesh:
    {
        const auto* value = Read<RHISoftwareDispatchMeshArguments>(arguments, bytes);
        return value && state.renderingOpen && bound(RHIBindPoint::Graphics, true) &&
               VulkanApi::vkCmdDrawMeshTasksEXT &&
               m_resources.GetMeshShaderCapabilities().SupportsDispatch(value->x, value->y, value->z);
    }
    case RHISoftwareCommandType::DispatchMeshIndirect:
    {
        const auto* value = Read<RHISoftwareDispatchMeshIndirectArguments>(arguments, bytes);
        const auto capabilities = m_resources.GetMeshShaderCapabilities();
        return value && state.renderingOpen && bound(RHIBindPoint::Graphics, true) && capabilities.meshShader &&
               capabilities.meshIndirect && VulkanApi::vkCmdDrawMeshTasksIndirectEXT &&
               indirect(value->arguments, value->byteOffset, sizeof(RHIDispatchMeshIndirectArguments));
    }
    case RHISoftwareCommandType::CopyBuffer:
    {
        const auto* value = Read<RHISoftwareCopyBufferArguments>(arguments, bytes);
        if (!value || !value->source.size || value->source.size != value->destination.size)
        {
            return false;
        }
        const auto source = m_resources.m_resourceTable.Resolve(value->source.buffer);
        const auto destination = m_resources.m_resourceTable.Resolve(value->destination.buffer);
        return source.IsValid() && destination.IsValid() && source.buffer != destination.buffer &&
               value->source.offset <= source.bytes && value->source.size <= source.bytes - value->source.offset &&
               value->destination.offset <= destination.bytes &&
               value->destination.size <= destination.bytes - value->destination.offset;
    }
    // Vulkan has no native implementation for the remaining bool operations.
    // Return false before recording, preserving the caller's explicit fallback.
    default:
        return false;
    }
}

void VulkanSoftwareCommandValidator::ApplyRecordedState(RHISoftwareCommandType type, const void* arguments,
                                                        size_t bytes, RHISoftwareEncoderState& state) const
{
    bool endsRendering = false;
    switch (type)
    {
    case RHISoftwareCommandType::BindRenderTargets:
    {
        const auto* value = Read<RHISoftwareBindRenderTargetsArguments>(arguments, bytes);
        if (value && value->binding.IsValid() &&
            m_resources.m_renderTargetTables[m_resources.m_frameIndex].Resolve(value->binding.backend).IsValid())
        {
            state.renderingOpen = true;
            state.renderTargets = value->binding;
        }
        break;
    }
    case RHISoftwareCommandType::ResourceBarriers:
    {
        const auto* value = Read<RHISoftwareResourceBarriersArguments>(arguments, bytes);
        endsRendering = value && !value->barriers.IsEmpty();
        break;
    }
    case RHISoftwareCommandType::UavBarrier:
    {
        const auto* value = Read<RHISoftwareUavBarrierArguments>(arguments, bytes);
        endsRendering = value && !value->textures.empty();
        break;
    }
    case RHISoftwareCommandType::UavBarrierBuffers:
    {
        const auto* value = Read<RHISoftwareUavBarrierBuffersArguments>(arguments, bytes);
        endsRendering = value && !value->buffers.empty();
        break;
    }
    case RHISoftwareCommandType::CopyBuffer:
        endsRendering = true; // The bool operation was validated and appended.
        break;
    case RHISoftwareCommandType::CopyResource:
    {
        const auto* value = Read<RHISoftwareCopyResourceArguments>(arguments, bytes);
        if (!value)
        {
            break;
        }
        const auto source = m_resources.m_resourceTable.Resolve(value->source);
        const auto destination = m_resources.m_resourceTable.Resolve(value->destination);
        endsRendering = source.IsValid() && destination.IsValid() && source.width == destination.width &&
                        source.height == destination.height &&
                        source.depthOrArraySize == destination.depthOrArraySize &&
                        source.mipLevels == destination.mipLevels && source.format == destination.format &&
                        source.is3D == destination.is3D;
        break;
    }
    case RHISoftwareCommandType::ClearUnorderedAccess:
    {
        const auto* value = Read<RHISoftwareClearUnorderedAccessArguments>(arguments, bytes);
        endsRendering = value && value->view.dim != RHIBindingDesc::Dim::Buffer &&
                        m_resources.m_resourceTable.Resolve(value->view.resource).IsValid();
        break;
    }
    case RHISoftwareCommandType::CopyToReadback:
    {
        const auto* value = Read<RHISoftwareCopyToReadbackArguments>(arguments, bytes);
        endsRendering = value && value->readback.IsValid() && value->slice < value->readback.sliceCount &&
                        m_resources.m_resourceTable.Resolve(value->source).IsValid() &&
                        m_resources.m_resourceTable.Resolve(value->readback.buffer).IsValid();
        break;
    }
    case RHISoftwareCommandType::CopyVolumeToReadback:
    {
        const auto* value = Read<RHISoftwareCopyVolumeToReadbackArguments>(arguments, bytes);
        if (!value || !value->readback.IsValid())
        {
            break;
        }
        const auto image = m_resources.m_resourceTable.Resolve(value->source);
        const uint32_t pixelBytes = RHIFormatBytes(value->readback.format);
        endsRendering = image.IsValid() && image.is3D && value->sourceSubresource < image.mipLevels &&
                        m_resources.m_resourceTable.Resolve(value->readback.buffer).IsValid() && pixelBytes &&
                        value->readback.rowPitch % pixelBytes == 0;
        break;
    }
    case RHISoftwareCommandType::CopyBufferToReadback:
    {
        const auto* value = Read<RHISoftwareCopyBufferToReadbackArguments>(arguments, bytes);
        if (!value || !value->readback.IsValid())
        {
            break;
        }
        const auto source = m_resources.m_resourceTable.Resolve(value->source);
        endsRendering = source.IsValid() && value->sourceOffset < source.bytes &&
                        m_resources.m_resourceTable.Resolve(value->readback.buffer).IsValid();
        break;
    }
    default:
        break;
    }
    if (endsRendering)
    {
        state.renderingOpen = false;
        state.renderTargets = {};
    }
}
