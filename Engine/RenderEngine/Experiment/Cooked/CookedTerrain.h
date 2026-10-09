#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace experiment::cooked
{
    inline constexpr std::uint32_t kCookedTerrainMagic = 0x5442524eu;
    inline constexpr std::uint32_t kCookedTerrainVersion = 2u;
    inline constexpr std::size_t kCookedTerrainMaxLayers = 4u;
    inline constexpr std::uint64_t kCookedTerrainMaxBytes = 1024ull * 1024ull * 1024ull;
    // Shared producer/reader admission budget for the current terrain CPU layout.
    // Includes input bytes, decoded images, vertex/index staging and mesh copies.
    inline constexpr std::uint64_t kCookedTerrainMaxWorkingBytes = 1024ull * 1024ull * 1024ull;
    inline constexpr std::uint64_t kCookedTerrainVertexBytes = 96u;

    struct CookedTerrainLayerView
    {
        std::uint32_t id{};
        float tiling{};
        std::string_view name;
        std::string_view diffuseReference;
        std::span<const std::byte> gray;
        std::span<const std::byte> texture;
    };

    // Borrowed, zero-allocation parser result. Heights contain little-endian
    // IEEE-754 bit patterns; gray contains exact authored gray8 samples.
    struct CookedTerrainView
    {
        std::uint32_t terrainId{};
        std::uint32_t width{};
        std::uint32_t height{};
        float minHeight{};
        float maxHeight{};
        std::uint32_t layerCount{};
        std::span<const std::byte> heights;
        std::array<CookedTerrainLayerView, kCookedTerrainMaxLayers> layers;
    };

    // 48-byte LE header: magic/version u32, totalBytes u64, terrainId/width/
    // height/layers/minBits/maxBits/headerBytes/reserved u32. Tight height bytes
    // follow. Each layer has a 24-byte header: id/tilingBits/nameBytes/refBytes
    // u32, textureBytes u64, then UTF-8 name/reference, gray8 and embedded CECT.
    // No native structures, external source paths or SDK decoders are required.
    [[nodiscard]] bool ReadCookedTerrain(std::span<const std::byte> bytes,
        CookedTerrainView& terrain, std::string& failure);
    [[nodiscard]] bool EncodeCookedTerrain(const CookedTerrainView& terrain,
        std::vector<std::byte>& bytes, std::string& failure);
    [[nodiscard]] float CookedTerrainHeight(std::span<const std::byte> bytes,
        std::size_t index) noexcept;
}
