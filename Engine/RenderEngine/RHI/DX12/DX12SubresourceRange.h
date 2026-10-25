#pragma once
#include "../RHIResourceState.h"
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Platform/WindowsSDK/Creator.GraphicsSDK.h"
#else
import "Creator.GraphicsSDK.h";
#endif

inline D3D12_RESOURCE_BARRIER_FLAGS DX12TransitionFlags(RHITransitionPhase phase)
{
    switch (phase)
    {
    case RHITransitionPhase::Begin: return D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY;
    case RHITransitionPhase::End: return D3D12_RESOURCE_BARRIER_FLAG_END_ONLY;
    default: return D3D12_RESOURCE_BARRIER_FLAG_NONE;
    }
}

inline RHITextureAspect DX12TextureAspects(const D3D12_RESOURCE_DESC& description)
{
    switch (description.Format)
    {
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:
        return RHITextureAspect::Depth | RHITextureAspect::Stencil;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_D32_FLOAT:
        return RHITextureAspect::Depth;
    default:
        return (description.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) != 0 ? RHITextureAspect::Depth
                                                                                  : RHITextureAspect::Color;
    }
}

inline void AppendDX12TextureTransitions(ID3D12Resource& resource, D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after, const RHITextureSubresourceRange& range,
                                         std::vector<D3D12_RESOURCE_BARRIER>& barriers,
                                         D3D12_RESOURCE_BARRIER_FLAGS flags = D3D12_RESOURCE_BARRIER_FLAG_NONE)
{
    const D3D12_RESOURCE_DESC description = resource.GetDesc();
    const uint32_t layers = description.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
                                ? 1u
                                : static_cast<uint32_t>(description.DepthOrArraySize);
    const RHITextureAspect aspects = DX12TextureAspects(description);
    RHITextureSubresourceRange resolved;
    if (!ResolveRHITextureSubresourceRange(range, description.MipLevels, layers, aspects, resolved))
    {
        throw std::invalid_argument("DX12 texture transition has an invalid subresource range");
    }
    if (before == after)
    {
        return;
    }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = flags;
    barrier.Transition.pResource = &resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    if (IsWholeRHITextureRange(resolved, description.MipLevels, layers, aspects))
    {
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers.push_back(barrier);
        return;
    }
    const uint32_t aspectBits = static_cast<uint8_t>(resolved.aspect);
    for (uint32_t plane = 0; plane < 2; ++plane)
    {
        const uint32_t planeBits =
            plane == 0 ? static_cast<uint8_t>(RHITextureAspect::Color) | static_cast<uint8_t>(RHITextureAspect::Depth)
                       : static_cast<uint8_t>(RHITextureAspect::Stencil);
        if ((aspectBits & planeBits) == 0)
        {
            continue;
        }
        for (uint32_t layer = resolved.firstLayer; layer < resolved.firstLayer + resolved.layerCount; ++layer)
        {
            for (uint32_t mip = resolved.firstMip; mip < resolved.firstMip + resolved.mipCount; ++mip)
            {
                barrier.Transition.Subresource =
                    mip + layer * description.MipLevels + plane * description.MipLevels * layers;
                barriers.push_back(barrier);
            }
        }
    }
}

inline void ValidateDX12BufferTransitionRange(ID3D12Resource& resource, const RHIBufferSubresourceRange& range)
{
    const D3D12_RESOURCE_DESC description = resource.GetDesc();
    RHIBufferSubresourceRange resolved;
    if (description.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER ||
        !ResolveRHIBufferSubresourceRange(range, description.Width, resolved) || resolved.offset != 0 ||
        resolved.size != description.Width)
    {
        // Legacy D3D12 buffer state is one subresource. The graph must normalize
        // transitions to the resource extent before emitting this command.
        throw std::invalid_argument("DX12 buffer state transitions require the complete buffer");
    }
}
