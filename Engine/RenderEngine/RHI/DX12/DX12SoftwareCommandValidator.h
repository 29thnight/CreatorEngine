#pragma once
#include "DX12DeviceResources.h"
#include "../RHISoftwareEncoder.h"

class DX12SoftwareCommandValidator final : public IRHISoftwareCommandValidator
{
  public:
    explicit DX12SoftwareCommandValidator(DX12DeviceResources& resources) : m_resources(resources) {}

    bool Validate(RHISoftwareCommandType type, const void* arguments, size_t bytes,
                  const RHISoftwareEncoderState& state) const override
    {
        const auto pipeline = m_resources.Resolve(state.boundPipeline);
        const auto bound = [&](RHIBindPoint point, bool mesh = false)
        {
            return pipeline.IsValid() && state.bindPoint == point &&
                   pipeline.computePipeline == (point == RHIBindPoint::Compute) && pipeline.meshPipeline == mesh;
        };
        const auto indirect = [&](RHIBufferHandle handle, uint64_t offset, uint64_t required)
        {
            const auto buffer = m_resources.DescribeBuffer(handle);
            return offset % 4 == 0 && buffer.IsValid() && buffer.allowIndirectArguments && offset <= buffer.bytes &&
                   required <= buffer.bytes - offset;
        };
        switch (type)
        {
        case RHISoftwareCommandType::SetViewport:
        {
            const auto* value = Read<RHISoftwareSetViewportArguments>(arguments, bytes);
            return value && std::isfinite(value->x) && std::isfinite(value->y) && value->width && value->height &&
                   value->x >= D3D12_VIEWPORT_BOUNDS_MIN && value->y >= D3D12_VIEWPORT_BOUNDS_MIN &&
                   double(value->x) + value->width <= D3D12_VIEWPORT_BOUNDS_MAX &&
                   double(value->y) + value->height <= D3D12_VIEWPORT_BOUNDS_MAX;
        }
        case RHISoftwareCommandType::SetScissor:
        {
            const auto* value = Read<RHISoftwareSetScissorArguments>(arguments, bytes);
            return value && value->x <= INT32_MAX && value->y <= INT32_MAX && value->width <= INT32_MAX - value->x &&
                   value->height <= INT32_MAX - value->y;
        }
        case RHISoftwareCommandType::BeginRasterWithoutAttachments:
        {
            const auto* value = Read<RHISoftwareBeginRasterWithoutAttachmentsArguments>(arguments, bytes);
            return value && value->width && value->height;
        }
        case RHISoftwareCommandType::SetRootUnorderedBuffer:
        {
            const auto* value = Read<RHISoftwareSetRootUnorderedBufferArguments>(arguments, bytes);
            if (!value || !value->value.size || value->value.offset % 4 ||
                !m_resources.ResolveGpuAddress(value->value.buffer))
            {
                return false;
            }
            auto* resource = m_resources.Resolve(value->value.buffer);
            if (!resource)
            {
                return false;
            }
            const auto desc = resource->GetDesc();
            return (desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) && value->value.offset < desc.Width &&
                   value->value.size <= desc.Width - value->value.offset;
        }
        case RHISoftwareCommandType::DrawIndirect:
        {
            const auto* value = Read<RHISoftwareDrawIndirectArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics) && m_resources.GetDrawIndirectSignature() &&
                   indirect(value->arguments, value->byteOffset, sizeof(RHIDrawIndirectArguments));
        }
        case RHISoftwareCommandType::DrawIndexedIndirect:
        {
            const auto* value = Read<RHISoftwareDrawIndexedIndirectArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics) && m_resources.GetDrawIndexedIndirectSignature() &&
                   indirect(value->arguments, value->byteOffset, sizeof(RHIDrawIndexedIndirectArguments));
        }
        case RHISoftwareCommandType::DispatchIndirect:
        {
            const auto* value = Read<RHISoftwareDispatchIndirectArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Compute) && m_resources.GetDispatchIndirectSignature() &&
                   indirect(value->arguments, value->byteOffset, sizeof(RHIDispatchIndirectArguments));
        }
        case RHISoftwareCommandType::DispatchMesh:
        {
            const auto* value = Read<RHISoftwareDispatchMeshArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics, true) &&
                   m_resources.GetMeshShaderCapabilities().SupportsDispatch(value->x, value->y, value->z);
        }
        case RHISoftwareCommandType::DispatchMeshIndirect:
        {
            const auto* value = Read<RHISoftwareDispatchMeshIndirectArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics, true) &&
                   m_resources.GetMeshShaderCapabilities().meshIndirect &&
                   m_resources.GetDispatchMeshIndirectSignature() &&
                   indirect(value->arguments, value->byteOffset, sizeof(RHIDispatchMeshIndirectArguments));
        }
        case RHISoftwareCommandType::DispatchMeshIndirectBatch:
        {
            const auto* value = Read<RHISoftwareDispatchMeshIndirectBatchArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics, true) && value->count && !value->roots.empty() &&
                   value->roots.size() <= 16 &&
                   indirect(value->arguments, value->byteOffset, (value->roots.size() * 8 + 12) * value->count) &&
                   m_resources.GetMaterialBatchSignature(pipeline.signature, value->roots, false);
        }
        case RHISoftwareCommandType::DrawIndexedIndirectBatch:
        {
            const auto* value = Read<RHISoftwareDrawIndexedIndirectBatchArguments>(arguments, bytes);
            return value && bound(RHIBindPoint::Graphics) && value->count && !value->roots.empty() &&
                   value->roots.size() <= 16 &&
                   indirect(value->arguments, value->byteOffset, (value->roots.size() * 8 + 36) * value->count) &&
                   m_resources.GetMaterialBatchSignature(pipeline.signature, value->roots, true);
        }
        case RHISoftwareCommandType::CopyBuffer:
        {
            const auto* value = Read<RHISoftwareCopyBufferArguments>(arguments, bytes);
            if (!value || !value->source.size || value->source.size != value->destination.size)
            {
                return false;
            }
            auto* source = m_resources.Resolve(value->source.buffer);
            auto* destination = m_resources.Resolve(value->destination.buffer);
            if (!source || !destination || source == destination)
            {
                return false;
            }
            const auto sourceBytes = source->GetDesc().Width;
            const auto destinationBytes = destination->GetDesc().Width;
            return value->source.offset <= sourceBytes && value->source.size <= sourceBytes - value->source.offset &&
                   value->destination.offset <= destinationBytes &&
                   value->destination.size <= destinationBytes - value->destination.offset;
        }
        default:
            return false;
        }
    }

  private:
    template<typename T>
    static const T* Read(const void* value, size_t bytes)
    {
        return value && bytes == sizeof(T) ? static_cast<const T*>(value) : nullptr;
    }
    DX12DeviceResources& m_resources;
};
