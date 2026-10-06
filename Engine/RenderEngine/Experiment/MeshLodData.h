#pragma once

#include "MeshletData.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace experiment
{
    inline constexpr std::uint32_t kMeshLodBuilderVersion = 1u;
    inline constexpr std::uint32_t kMeshLodMaxLevels = 7u; // Coarse levels; LOD0 is the original mesh.
    inline constexpr std::uint32_t kMeshLodLockBorder = 1u;

    struct MeshLodBuildSettings final
    {
        std::uint32_t builderVersion{ kMeshLodBuilderVersion };
        std::uint32_t meshoptimizerVersion{ kMeshletMeshoptimizerVersion };
        std::uint32_t levelCount{}; // Opt-in: zero does not generate LODs.
        float reductionRatio{ 0.5f };
        float targetRelativeError{ 0.01f };
        std::uint32_t flags{ kMeshLodLockBorder };
    };

    struct MeshLodLevel final
    {
        // Meshoptimizer's simplification error metric, in mesh-local position
        // units, rounded outward and monotonic across this chain. It is not a
        // certified Hausdorff distance. Each level is simplified from LOD0.
        float geometricError{};
        // References unchanged LOD0 vertices. Base mesh indices are never replaced.
        std::vector<std::uint32_t> indices{};
        // Primitive remap is this level's own finalized triangle ordinal, NOT
        // an original LOD0 primitive ID. All descriptor offsets are level-local.
        MeshletPayload meshlets{};
    };

    struct MeshLodChain final
    {
        MeshLodBuildSettings settings{};
        MeshletGeometryDigest geometryDigest{};
        std::vector<MeshLodLevel> levels{};

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return levels.empty() && geometryDigest == MeshletGeometryDigest{};
        }
    };

    static_assert(std::is_trivially_copyable_v<MeshLodBuildSettings>);
    static_assert(sizeof(MeshLodBuildSettings) == 24 && alignof(MeshLodBuildSettings) == 4);
    static_assert(offsetof(MeshLodBuildSettings, reductionRatio) == 12);
}
