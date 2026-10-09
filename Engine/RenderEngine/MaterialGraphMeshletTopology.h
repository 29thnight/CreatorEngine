#pragma once

#include "RHI/IRenderDeviceServices.h"
#include <algorithm>
#include <array>
#include <limits>
#include <vector>

namespace material_graph
{
    // Lattice uses fine derivatives and equal-depth material passes. Preserve
    // source primitive order AND corner order while partitioning the sealed
    // chunk; spatial meshoptimizer reordering changes raster helper quads.
    inline bool BuildSceneMeshletTopology(const RHIModelMeshView& geometry,
        std::vector<uint32_t>& words, std::string& error)
    {
        if (!geometry.vertexData || !geometry.indexData || !geometry.vertexStride ||
            geometry.vertexBytes % geometry.vertexStride != 0 || geometry.indexCount % 3 != 0)
        {
            error = "LX meshlet topology requires complete indexed triangles.";
            return false;
        }
        const auto vertexCount = geometry.vertexBytes / geometry.vertexStride;
        std::vector<uint32_t> descriptors, remaps, triangles;
        std::vector<uint32_t> local;
        local.reserve(64);
        uint32_t triangleCount{};
        const auto flush = [&]()
        {
            if (!triangleCount)
            {
                return;
            }
            descriptors.insert(descriptors.end(), {static_cast<uint32_t>(remaps.size()),
                static_cast<uint32_t>(triangles.size()) - triangleCount * 3,
                static_cast<uint32_t>(local.size()), triangleCount});
            remaps.insert(remaps.end(), local.begin(), local.end());
            local.clear();
            triangleCount = 0;
        };
        for (uint32_t i = 0; i < geometry.indexCount; i += 3)
        {
            const std::array<uint32_t, 3> triangle{geometry.indexData[i],
                geometry.indexData[i + 1], geometry.indexData[i + 2]};
            uint32_t additions{};
            for (uint32_t corner = 0; corner < 3; ++corner)
            {
                const auto index = triangle[corner];
                if (index >= vertexCount)
                {
                    error = "LX meshlet topology contains an out-of-range vertex.";
                    return false;
                }
                if (std::ranges::find(local, index) == local.end() &&
                    std::find(triangle.begin(), triangle.begin() + corner, index) == triangle.begin() + corner)
                {
                    ++additions;
                }
            }
            if (local.size() + additions > 64 || triangleCount == 126)
            {
                flush();
            }
            for (const auto index : triangle)
            {
                auto found = std::ranges::find(local, index);
                if (found == local.end())
                {
                    local.push_back(index);
                    found = local.end() - 1;
                }
                triangles.push_back(static_cast<uint32_t>(found - local.begin()));
            }
            ++triangleCount;
        }
        flush();
        const uint64_t triangleBase = 4ull + descriptors.size() + remaps.size();
        if (triangleBase + triangles.size() > (std::numeric_limits<uint32_t>::max)())
        {
            error = "LX meshlet topology exceeds 32-bit shader offsets.";
            return false;
        }
        // uint word offsets: count, descriptors, vertex remaps, triangle indices.
        words = {static_cast<uint32_t>(descriptors.size() / 4), 4u,
            static_cast<uint32_t>(4 + descriptors.size()), static_cast<uint32_t>(triangleBase)};
        words.insert(words.end(), descriptors.begin(), descriptors.end());
        words.insert(words.end(), remaps.begin(), remaps.end());
        words.insert(words.end(), triangles.begin(), triangles.end());
        error.clear();
        return true;
    }
}
