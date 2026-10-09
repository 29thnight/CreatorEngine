#pragma once

#include "../../TextureImage.h"
#include "TextureImportSettings.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace experiment::cooked
{
    inline constexpr std::uint32_t kCookedTextureSchemaVersion = 2u;
    inline constexpr std::uint32_t kCookedTextureRepresentationVersion = 2u;
    inline constexpr std::uint32_t kCookedTextureHeaderBytes = 64u;
    inline constexpr std::uint32_t kCookedTextureSubresourceBytes = 40u;
    inline constexpr std::uint32_t kCookedTextureMaxDimension = 16384u;
    inline constexpr std::uint32_t kCookedTextureMaxArraySize = 2048u;
    inline constexpr std::uint64_t kCookedTextureMaxBytes = 1024ull * 1024ull * 1024ull;

    struct CookedTextureInfo final
    {
        bool hasAlpha{};
        bool colorSpaceLocked{};
    };

    // CECT is little-endian; no native struct, DXGI value, pointer or padding is
    // persisted. 64-byte header: magic, schema, headerBytes, representation,
    // format, flags (cube=1, alpha=2, colorSpaceLocked=4), width, height, mipLevels, arraySize,
    // subresourceCount, entryBytes (all u32), payloadOffset and totalBytes (u64).
    // Each 40-byte table entry: width/height u32; tight rowPitch, slicePitch,
    // absolute payload offset, payload size u64. Order is array item then mip.
    // The portable desktop baseline requires BC base dimensions divisible by 4;
    // mip tails may be smaller. No OPTIONS8 unaligned-block capability is assumed.
    // Payloads are contiguous and exact; trailing bytes and overlapping ranges
    // are rejected. A complete validated table precedes any pixel allocation.
    [[nodiscard]] bool EncodeCookedTexture(TextureImageView image,
        std::vector<std::byte>& artifact, std::string& failure, CookedTextureInfo info = {});
    [[nodiscard]] bool DecodeCookedTexture(std::span<const std::byte> artifact,
        TextureImage& image, std::string& failure, CookedTextureInfo* info = nullptr);

    // Offline only. Implemented separately in TextureCooker.cpp with DirectXTex.
    // Desktop DX12/Vulkan BC output; no ASTC/mobile fallback or GPU encoder.
    [[nodiscard]] bool CookTexture(std::span<const std::byte> source,
        const TextureImportSettings& settings, std::vector<std::byte>& artifact, std::string& failure);
}
