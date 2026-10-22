#pragma once
#include "RHIFormat.h"

enum class RHITextureAspect : uint8_t
{
    All = 0,
    Color = 1,
    Depth = 2,
    Stencil = 4
};

struct RHISubresourceCapabilities
{
    bool textureRanges{false};
    bool bufferRanges{false};
    bool separateDepthStencil{false};
};

constexpr RHITextureAspect operator|(RHITextureAspect left, RHITextureAspect right)
{
    return static_cast<RHITextureAspect>(static_cast<uint8_t>(left) | static_cast<uint8_t>(right));
}

constexpr RHITextureAspect RHITextureAspects(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::D24UnormS8Uint:
    case RHIFormat::D32FloatS8Uint:
        return RHITextureAspect::Depth | RHITextureAspect::Stencil;
    case RHIFormat::D16Unorm:
    case RHIFormat::D32Float:
        return RHITextureAspect::Depth;
    case RHIFormat::Unknown:
        return RHITextureAspect::All;
    default:
        return RHITextureAspect::Color;
    }
}

// The sentinel means the remaining subresources, including for legacy whole-image
// declarations. Zero counts are invalid; they must never silently become whole-image.
struct RHITextureSubresourceRange
{
    uint32_t firstMip{0};
    uint32_t mipCount{UINT32_MAX};
    uint32_t firstLayer{0};
    uint32_t layerCount{UINT32_MAX};
    RHITextureAspect aspect{RHITextureAspect::All};

    bool operator==(const RHITextureSubresourceRange&) const = default;
};

struct RHIBufferSubresourceRange
{
    uint64_t offset{0};
    uint64_t size{UINT64_MAX};

    bool operator==(const RHIBufferSubresourceRange&) const = default;
};

constexpr bool ResolveRHITextureSubresourceRange(const RHITextureSubresourceRange& range, uint32_t mipLevels,
                                                 uint32_t arrayLayers, RHITextureAspect supportedAspects,
                                                 RHITextureSubresourceRange& resolved)
{
    const uint8_t supported = static_cast<uint8_t>(supportedAspects);
    const uint8_t requested = range.aspect == RHITextureAspect::All ? supported : static_cast<uint8_t>(range.aspect);
    if (mipLevels == 0 || arrayLayers == 0 || range.firstMip >= mipLevels || range.firstLayer >= arrayLayers ||
        requested == 0 || (requested & ~supported) != 0)
    {
        return false;
    }
    const uint32_t mips = range.mipCount == UINT32_MAX ? mipLevels - range.firstMip : range.mipCount;
    const uint32_t layers = range.layerCount == UINT32_MAX ? arrayLayers - range.firstLayer : range.layerCount;
    if (mips == 0 || layers == 0 || mips > mipLevels - range.firstMip || layers > arrayLayers - range.firstLayer)
    {
        return false;
    }
    resolved = {range.firstMip, mips, range.firstLayer, layers, static_cast<RHITextureAspect>(requested)};
    return true;
}

constexpr bool ResolveRHIBufferSubresourceRange(const RHIBufferSubresourceRange& range, uint64_t bytes,
                                                RHIBufferSubresourceRange& resolved)
{
    if (bytes == 0 || range.offset >= bytes)
    {
        return false;
    }
    const uint64_t size = range.size == UINT64_MAX ? bytes - range.offset : range.size;
    if (size == 0 || size > bytes - range.offset)
    {
        return false;
    }
    resolved = {range.offset, size};
    return true;
}

constexpr bool IsWholeRHITextureRange(const RHITextureSubresourceRange& resolved, uint32_t mipLevels,
                                      uint32_t arrayLayers, RHITextureAspect aspects)
{
    return resolved.firstMip == 0 && resolved.mipCount == mipLevels && resolved.firstLayer == 0 &&
           resolved.layerCount == arrayLayers && resolved.aspect == aspects;
}
