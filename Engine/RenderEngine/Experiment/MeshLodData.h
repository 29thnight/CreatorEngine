#include "../../Containers/ContainerAPI.h"
#pragma once

#include "MeshletData.h"

#include <stddef.h>
#include <stdint.h>
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace experiment
{
    inline constexpr std::uint32_t kMeshLodBuilderVersion = 4u;
    inline constexpr std::uint32_t kMeshLodMaxLevels = 7u; // Coarse levels; LOD0 is the original mesh.
    inline constexpr std::uint32_t kMeshClusterMaxGroups = (1u << (kMeshLodMaxLevels + 1)) - 1u;
    inline constexpr std::uint32_t kMeshLodLockBorder = 1u;

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MeshLodBuildSettings final
    {
        std::uint32_t builderVersion{kMeshLodBuilderVersion};
        std::uint32_t meshoptimizerVersion{kMeshletMeshoptimizerVersion};
        std::uint32_t levelCount{}; // Opt-in: zero does not generate LODs.
        float reductionRatio{0.5f};
        float targetRelativeError{0.01f};
        std::uint32_t flags{kMeshLodLockBorder};
    };

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MeshLodLevel final
    {
        // Meshoptimizer's simplification error metric, in mesh-local position
        // units, rounded outward. Per-group errors are monotonic toward parents. It is not a
        // certified Hausdorff distance for rigid legacy groups. Skinned groups add
        // bidirectional triangle correspondence certificates. Levels aggregate equal depths.
        float geometricError{};
        // References unchanged LOD0 vertices. Base mesh indices are never replaced.
        ce::dynamic_array<std::uint32_t> indices{};
        // Primitive remap is this level's own finalized triangle ordinal, NOT
        // an original LOD0 primitive ID. All descriptor offsets are level-local.
        MeshletPayload meshlets{};
    };

    // Spatial replacement DAG, in child-before-parent order. The cooker emits
    // a disjoint binary tree (one parent per group), a bounded acyclic subset of
    // the DAG contract. Parent topology replaces BOTH children atomically; patch
    // borders stay original so independently selected neighbouring cuts meet.
    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MeshClusterGroup final
    {
        std::uint32_t firstMeshlet{}, meshletCount{}, childGroup{UINT32_MAX}, secondChild{UINT32_MAX};
        std::uint32_t parentGroup{UINT32_MAX}, lodIndex{}, primitiveBase{};
        float geometricError{};
        float sphere[4]{}; // Conservative original-patch bound, shared by every chunk.
        // Skinned certificate: float weighted-position bound, float weight-L1 bound,
        // tag (1=certificate, 3=also includes the zero-weight identity transform).
        std::uint32_t levelFirstMeshlet{}, reserved[3]{};
        friend bool operator==(const MeshClusterGroup&, const MeshClusterGroup&) = default;
    };
    static_assert(sizeof(MeshClusterGroup) == 64);
    static_assert(offsetof(MeshClusterGroup, parentGroup) == 16);
    static_assert(offsetof(MeshClusterGroup, geometricError) == 28);
    static_assert(offsetof(MeshClusterGroup, sphere) == 32);
    static_assert(offsetof(MeshClusterGroup, levelFirstMeshlet) == 48);

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MeshClusterHierarchy final
    {
        std::uint32_t groupCount{}, rootGroup{UINT32_MAX};
        ce::dynamic_array<MeshClusterGroup> groups{};
        [[nodiscard]] std::uint64_t GpuBytes() const noexcept
        {
            return groupCount ? 16ull + groups.size() * sizeof(MeshClusterGroup) : 0ull;
        }
    };

    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] MeshLodChain final
    {
        MeshLodBuildSettings settings{};
        MeshletGeometryDigest geometryDigest{};
        ce::dynamic_array<MeshLodLevel> levels{};
        ce::dynamic_array<MeshClusterGroup> groups{}; // Spatial DAG; children precede parents, last group is root.

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return levels.empty() && groups.empty() && geometryDigest == MeshletGeometryDigest{};
        }
    };

    static_assert(std::is_trivially_copyable_v<MeshLodBuildSettings>);
    static_assert(sizeof(MeshLodBuildSettings) == 24 && alignof(MeshLodBuildSettings) == 4);
    static_assert(offsetof(MeshLodBuildSettings, reductionRatio) == 12);
} // namespace experiment
