#pragma once
#include <stdint.h>
#include "../Utility_Framework/StandardLibrary.h"

namespace material_graph
{
// Frame-local primitive slots: zero is background, UINT32_MAX is the reduction
// sentinel. Each meshlet owns 128 slots; slots 126/127 can never name a triangle.
// Low word = address, high word = standard-Z float bits. Unsigned uint64 min
// orders finite nonnegative depth first, then the immutable frame address.
struct SceneVisibilityContract final
{
    static constexpr uint32_t TriangleBits = 7;
    static constexpr uint64_t EmptyKey = UINT64_MAX;
    static bool Reserve(uint32_t meshlets, uint32_t& next, uint32_t& begin, uint32_t& end)
    {
        const uint64_t limit = uint64_t(next) + uint64_t(meshlets) * 128;
        if (!next || !meshlets || limit > UINT32_MAX)
            return false;
        begin = next;
        end = static_cast<uint32_t>(limit);
        next = end;
        return true;
    }
    static uint32_t Address(uint32_t begin, uint32_t end, uint32_t local)
    {
        if (!begin || end <= begin || local >= end - begin || (local & 127) >= 126)
            return 0;
        return begin + local;
    }
    static uint64_t Key(float depth, uint32_t address)
    {
        if (!address || address == UINT32_MAX || !(depth >= 0.f && depth < 1.f))
            return EmptyKey;
        // Match HW Less with a depth clear of 1: far-plane equality is background.
        // Canonicalize -0; otherwise unsigned ordering would put it after +1.
        return (uint64_t(std::bit_cast<uint32_t>(depth == 0.f ? 0.f : depth)) << 32) | address;
    }
};
} // namespace material_graph
