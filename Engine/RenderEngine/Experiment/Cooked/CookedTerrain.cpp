#include "CookedTerrain.h"
#include "CookedTexture.h"

#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>

namespace experiment::cooked
{
    static_assert(sizeof(float) == 4u && std::numeric_limits<float>::is_iec559);

    namespace
    {
        constexpr std::size_t TerrainHeaderBytes = 48u;
        constexpr std::size_t TerrainLayerHeaderBytes = 24u;

        std::uint64_t TerrainRead(std::span<const std::byte> bytes, std::size_t offset, std::size_t count)
        {
            std::uint64_t value{};
            for (std::size_t index = 0u; index < count; ++index)
            {
                value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8u);
            }
            return value;
        }

        void TerrainWrite(std::vector<std::byte>& bytes, std::size_t offset, std::uint64_t value, std::size_t count)
        {
            for (std::size_t index = 0u; index < count; ++index)
            {
                bytes[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xffu);
            }
        }

        bool TerrainShape(const CookedTerrainView& terrain)
        {
            return terrain.width > 0u && terrain.height > 0u
                && terrain.width <= kCookedTextureMaxDimension && terrain.height <= kCookedTextureMaxDimension
                && terrain.layerCount <= kCookedTerrainMaxLayers && std::isfinite(terrain.minHeight)
                && std::isfinite(terrain.maxHeight) && terrain.minHeight <= terrain.maxHeight;
        }
    }

    float CookedTerrainHeight(std::span<const std::byte> bytes, std::size_t index) noexcept
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(TerrainRead(bytes, index * 4u, 4u)));
    }

    bool ReadCookedTerrain(std::span<const std::byte> bytes, CookedTerrainView& terrain, std::string& failure)
    {
        terrain = {};
        failure.clear();
        const auto fail = [&](const char* message)
        {
            failure = message;
            return false;
        };
        if (bytes.size() < TerrainHeaderBytes || bytes.size() > kCookedTerrainMaxBytes
            || TerrainRead(bytes, 0u, 4u) != kCookedTerrainMagic
            || TerrainRead(bytes, 4u, 4u) != kCookedTerrainVersion
            || TerrainRead(bytes, 8u, 8u) != bytes.size()
            || TerrainRead(bytes, 40u, 4u) != TerrainHeaderBytes || TerrainRead(bytes, 44u, 4u) != 0u)
        {
            return fail("Terrain requires a complete neutral v2 artifact; recook the terrain.");
        }
        CookedTerrainView result;
        result.terrainId = static_cast<std::uint32_t>(TerrainRead(bytes, 16u, 4u));
        result.width = static_cast<std::uint32_t>(TerrainRead(bytes, 20u, 4u));
        result.height = static_cast<std::uint32_t>(TerrainRead(bytes, 24u, 4u));
        result.layerCount = static_cast<std::uint32_t>(TerrainRead(bytes, 28u, 4u));
        result.minHeight = std::bit_cast<float>(static_cast<std::uint32_t>(TerrainRead(bytes, 32u, 4u)));
        result.maxHeight = std::bit_cast<float>(static_cast<std::uint32_t>(TerrainRead(bytes, 36u, 4u)));
        if (!TerrainShape(result))
        {
            return fail("Terrain dimensions, layer count or height bounds are invalid.");
        }
        const auto pixels = static_cast<std::uint64_t>(result.width) * result.height;
        const auto indices = static_cast<std::uint64_t>(result.width - 1u) * (result.height - 1u) * 6u;
        // Shape limits above make these products uint64-safe. Retain the wire
        // buffer, height/normal/weight/mask vectors, both mesh staging copies,
        // and a conservative embedded-image payload allowance simultaneously.
        std::uint64_t workingBytes = bytes.size()
            + pixels * (4u + 12u + result.layerCount * 5u + kCookedTerrainVertexBytes * 2u)
            + indices * 8u;
        if (workingBytes > kCookedTerrainMaxWorkingBytes)
        {
            return fail("Terrain derived CPU working set exceeds its 1 GiB admission budget.");
        }
        std::size_t offset = TerrainHeaderBytes;
        if (pixels * 4u > bytes.size() - offset)
        {
            return fail("Terrain height payload is truncated.");
        }
        result.heights = bytes.subspan(offset, static_cast<std::size_t>(pixels * 4u));
        offset += result.heights.size();
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            if (!std::isfinite(CookedTerrainHeight(result.heights, index)))
            {
                return fail("Terrain height payload contains a non-finite value.");
            }
        }
        for (std::size_t index = 0u; index < result.layerCount; ++index)
        {
            if (bytes.size() - offset < TerrainLayerHeaderBytes)
            {
                return fail("Terrain layer header is truncated.");
            }
            auto& layer = result.layers[index];
            layer.id = static_cast<std::uint32_t>(TerrainRead(bytes, offset, 4u));
            layer.tiling = std::bit_cast<float>(static_cast<std::uint32_t>(TerrainRead(bytes, offset + 4u, 4u)));
            const auto nameBytes = TerrainRead(bytes, offset + 8u, 4u);
            const auto referenceBytes = TerrainRead(bytes, offset + 12u, 4u);
            const auto textureBytes = TerrainRead(bytes, offset + 16u, 8u);
            offset += TerrainLayerHeaderBytes;
            if (!std::isfinite(layer.tiling) || layer.id == UINT32_MAX || nameBytes == 0u || nameBytes > 4096u
                || referenceBytes == 0u || referenceBytes > 32768u
                || textureBytes < kCookedTextureHeaderBytes || textureBytes > kCookedTextureMaxBytes
                || nameBytes + referenceBytes + pixels + textureBytes > bytes.size() - offset)
            {
                return fail("Terrain layer values or bounded payload lengths are invalid.");
            }
            if (textureBytes > kCookedTerrainMaxWorkingBytes - workingBytes)
            {
                return fail("Terrain decoded images exceed its 1 GiB working-set budget.");
            }
            workingBytes += textureBytes;
            for (std::size_t previous = 0u; previous < index; ++previous)
            {
                if (result.layers[previous].id == layer.id)
                {
                    return fail("Terrain layer identity is duplicated.");
                }
            }
            layer.name = { reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::size_t>(nameBytes) };
            offset += static_cast<std::size_t>(nameBytes);
            layer.diffuseReference = { reinterpret_cast<const char*>(bytes.data() + offset), static_cast<std::size_t>(referenceBytes) };
            offset += static_cast<std::size_t>(referenceBytes);
            if (layer.name.find('\0') != std::string_view::npos || layer.diffuseReference.find('\0') != std::string_view::npos)
            {
                return fail("Terrain layer strings contain a null byte.");
            }
            layer.gray = bytes.subspan(offset, static_cast<std::size_t>(pixels));
            offset += static_cast<std::size_t>(pixels);
            layer.texture = bytes.subspan(offset, static_cast<std::size_t>(textureBytes));
            offset += static_cast<std::size_t>(textureBytes);
            if (layer.texture[0] != std::byte{ 'C' } || layer.texture[1] != std::byte{ 'E' }
                || layer.texture[2] != std::byte{ 'C' } || layer.texture[3] != std::byte{ 'T' })
            {
                return fail("Terrain diffuse payload is not a neutral cooked texture.");
            }
            if (!ValidateCookedTexture(layer.texture, failure))
            {
                return false;
            }
        }
        if (offset != bytes.size())
        {
            return fail("Terrain artifact contains trailing bytes.");
        }
        terrain = result;
        return true;
    }

    bool EncodeCookedTerrain(const CookedTerrainView& terrain, std::vector<std::byte>& bytes, std::string& failure)
    {
        bytes.clear();
        if (!TerrainShape(terrain))
        {
            failure = "Terrain source shape is invalid.";
            return false;
        }
        const auto pixels = static_cast<std::uint64_t>(terrain.width) * terrain.height;
        if (terrain.heights.size() != pixels * 4u)
        {
            failure = "Terrain source height byte count is invalid.";
            return false;
        }
        std::uint64_t total = TerrainHeaderBytes + terrain.heights.size();
        for (std::size_t index = 0u; index < terrain.layerCount; ++index)
        {
            const auto& layer = terrain.layers[index];
            if (layer.name.size() > 4096u || layer.diffuseReference.size() > 32768u
                || layer.gray.size() != pixels || layer.texture.size() > kCookedTextureMaxBytes)
            {
                failure = "Terrain source layer exceeds its byte limits.";
                return false;
            }
            total += TerrainLayerHeaderBytes + layer.name.size() + layer.diffuseReference.size()
                + layer.gray.size() + layer.texture.size();
        }
        if (total > kCookedTerrainMaxBytes)
        {
            failure = "Terrain artifact exceeds its byte budget.";
            return false;
        }
        try
        {
            std::vector<std::byte> encoded(static_cast<std::size_t>(total));
            TerrainWrite(encoded, 0u, kCookedTerrainMagic, 4u);
            TerrainWrite(encoded, 4u, kCookedTerrainVersion, 4u);
            TerrainWrite(encoded, 8u, total, 8u);
            TerrainWrite(encoded, 16u, terrain.terrainId, 4u);
            TerrainWrite(encoded, 20u, terrain.width, 4u);
            TerrainWrite(encoded, 24u, terrain.height, 4u);
            TerrainWrite(encoded, 28u, terrain.layerCount, 4u);
            TerrainWrite(encoded, 32u, std::bit_cast<std::uint32_t>(terrain.minHeight), 4u);
            TerrainWrite(encoded, 36u, std::bit_cast<std::uint32_t>(terrain.maxHeight), 4u);
            TerrainWrite(encoded, 40u, TerrainHeaderBytes, 4u);
            std::size_t offset = TerrainHeaderBytes;
            const auto append = [&](const void* source, std::size_t count)
            {
                if (count != 0u)
                {
                    std::memcpy(encoded.data() + offset, source, count);
                }
                offset += count;
            };
            append(terrain.heights.data(), terrain.heights.size());
            for (std::size_t index = 0u; index < terrain.layerCount; ++index)
            {
                const auto& layer = terrain.layers[index];
                TerrainWrite(encoded, offset, layer.id, 4u);
                TerrainWrite(encoded, offset + 4u, std::bit_cast<std::uint32_t>(layer.tiling), 4u);
                TerrainWrite(encoded, offset + 8u, layer.name.size(), 4u);
                TerrainWrite(encoded, offset + 12u, layer.diffuseReference.size(), 4u);
                TerrainWrite(encoded, offset + 16u, layer.texture.size(), 8u);
                offset += TerrainLayerHeaderBytes;
                append(layer.name.data(), layer.name.size());
                append(layer.diffuseReference.data(), layer.diffuseReference.size());
                append(layer.gray.data(), layer.gray.size());
                append(layer.texture.data(), layer.texture.size());
            }
            CookedTerrainView verified;
            if (!ReadCookedTerrain(encoded, verified, failure))
            {
                return false;
            }
            bytes = std::move(encoded);
            return true;
        }
        catch (const std::exception&)
        {
            failure = "Terrain artifact allocation failed.";
            return false;
        }
    }
}
