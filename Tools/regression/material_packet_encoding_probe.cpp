#include "../../Engine/RenderEngine/MaterialGraphPacketEncoding.h"
#include <array>
#include <iostream>
#include <stdexcept>

using material_graph::MaterialPacketEncoding;
static_assert(MaterialPacketEncoding::TileBits(193, 998, 448) == 13);
static_assert(MaterialPacketEncoding::TileBits(2, 8, 8) == 1);
static_assert(MaterialPacketEncoding::TileBits(0, 8, 8) == 0);
static_assert(MaterialPacketEncoding::TileBits(2, 0, 8) == 0);
static_assert(MaterialPacketEncoding::TileBits(65536, 2048, 2048) == 16);
static_assert(MaterialPacketEncoding::TileBits(65537, 2048, 2048) == 0);
static_assert(MaterialPacketEncoding::TileBits(UINT32_MAX, UINT32_MAX, UINT32_MAX) == 0);
static_assert(MaterialPacketEncoding::StorageElements(UINT32_MAX, 13) == 2147483648u);
static_assert(MaterialPacketEncoding::StorageElements(UINT32_MAX, 0) == UINT32_MAX);
static_assert(MaterialPacketEncoding::StorageElements(3, 13) == 2);

int main()
{
    uint64_t checks{};
    for (const uint32_t count : {2u, 3u, 193u, 4096u, 65536u, 65537u})
    {
        for (const uint32_t width : {1u, 7u, 8u, 9u, 998u, 2048u, 16384u})
        {
            for (const uint32_t height : {1u, 8u, 9u, 448u, 2048u, 16384u})
            {
                const auto bits = MaterialPacketEncoding::TileBits(count, width, height);
                const uint64_t tiles = ((uint64_t(width) + 7) / 8) * ((uint64_t(height) + 7) / 8);
                if (!bits)
                {
                    continue;
                }
                for (uint32_t record : {1u, count / 2, count - 1})
                {
                    for (uint32_t tile : {0u, static_cast<uint32_t>(tiles / 2), static_cast<uint32_t>(tiles - 1)})
                    {
                        const auto wide = (uint64_t(record) << bits) | tile;
                        const auto word = static_cast<uint32_t>(wide);
                        if (wide > UINT32_MAX || word >> bits != record || (word & ((1u << bits) - 1)) != tile)
                        {
                            throw std::runtime_error("Material packet encoding lost an ID.");
                        }
                        ++checks;
                    }
                }
            }
        }
    }
    std::cout << "MATERIAL_PACKET_ENCODING_OK checks=" << checks << " boundaryFallback=1 oddCapacity=1\n";
}
