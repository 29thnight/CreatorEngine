#pragma once
#include <algorithm>
#include <bit>
#include <cstdint>

namespace material_graph
{
    struct MaterialPacketEncoding
    {
        // Zero selects the original uint2 format. Both IDs remain exact; no
        // truncation is permitted when the view exceeds a single packed word.
        static constexpr uint32_t TileBits(uint32_t records, uint32_t width, uint32_t height)
        {
            if (!records || !width || !height)
            {
                return 0;
            }
            const uint64_t tiles = ((uint64_t(width) + 7) / 8) * ((uint64_t(height) + 7) / 8);
            const auto bits = std::max(1u, static_cast<uint32_t>(std::bit_width(tiles - 1)));
            return bits < 32 && bits + std::bit_width(records - 1) <= 32 ? bits : 0;
        }

        static constexpr uint32_t StorageElements(uint32_t capacity, uint32_t tileBits)
        {
            // Keep uint2 descriptors: adjacent logical packets use distinct
            // 32-bit lanes of one element, including an odd final capacity.
            return tileBits ? capacity / 2 + capacity % 2 : capacity;
        }
    };
}
