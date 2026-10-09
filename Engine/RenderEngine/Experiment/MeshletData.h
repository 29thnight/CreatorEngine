#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace experiment
{
    inline constexpr std::uint32_t kMeshletProfileVersion = 1u;
    inline constexpr std::uint32_t kMeshletBuilderVersion = 1u;
    inline constexpr std::uint32_t kMeshletMeshoptimizerVersion = 1020u;
    inline constexpr std::uint32_t kMeshletMaxVertices = 64u;
    inline constexpr std::uint32_t kMeshletMaxTriangles = 126u;

    // Profile/builder changes invalidate derived data independently of the model
    // file version. Every setting is included in the final geometry digest.
    struct MeshletBuildSettings final
    {
        std::uint32_t profileVersion{ kMeshletProfileVersion };
        std::uint32_t builderVersion{ kMeshletBuilderVersion };
        std::uint32_t meshoptimizerVersion{ kMeshletMeshoptimizerVersion };
        std::uint32_t maxVertices{ kMeshletMaxVertices };
        std::uint32_t maxTriangles{ kMeshletMaxTriangles };
        float coneWeight{};
    };

    inline constexpr std::uint32_t kMeshletConeValid = 1u << 0;
    inline constexpr std::uint32_t kMeshletRequiresDeformedBounds = 1u << 1;

    // Fixed scalar ABI, not a Mathematics vector ABI. Offsets refer to elements
    // in this Mesh's payload; triangleOffset is a byte offset (three per triangle).
    // Bounds are in mesh-local bind-pose space. A deformed instance must obtain
    // conservative deformed bounds before using them for rejection. ConeValid
    // is necessary, never sufficient: two-sided/mirrored/deformed draws also
    // need the runtime material/transform eligibility check.
    struct alignas(16) MeshletDescriptor final
    {
        std::uint32_t vertexOffset{};
        std::uint32_t triangleOffset{};
        std::uint32_t primitiveOffset{};
        std::uint32_t vertexCount{};
        std::uint32_t triangleCount{};
        std::uint32_t flags{};
        std::uint32_t reserved[2]{};
        float sphereCenter[3]{};
        float sphereRadius{};
        float coneApex[3]{};
        std::uint32_t reservedBounds{};
        float coneAxis[3]{};
        float coneCutoff{ 1.0f };
    };

    struct MeshletLodRange final
    {
        std::uint32_t firstMeshlet{};
        std::uint32_t meshletCount{};
    };

    using MeshletGeometryDigest = std::array<std::uint8_t, 32>;

    // Owned alongside the original indexed geometry in the same immutable Model
    // generation. No side cache, source GUID, timestamp, or runtime buffer handle.
    struct MeshletPayload final
    {
        MeshletBuildSettings settings{};
        MeshletGeometryDigest geometryDigest{};
        std::vector<MeshletDescriptor> descriptors{};
        std::vector<std::uint32_t> vertexRemap{};
        std::vector<std::uint8_t> triangleIndices{};
        // Final indexed triangle ordinal, not the source file's polygon ordinal.
        // Micro-triangle corners preserve that indexed triangle's exact order.
        std::vector<std::uint32_t> primitiveRemap{};
        MeshletLodRange lod0{};

        [[nodiscard]] bool HasMeshlets() const noexcept { return !descriptors.empty(); }

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return descriptors.empty() && vertexRemap.empty() && triangleIndices.empty()
                && primitiveRemap.empty() && lod0.firstMeshlet == 0 && lod0.meshletCount == 0
                && geometryDigest == MeshletGeometryDigest{};
        }
    };

    static_assert(sizeof(float) == 4);
    static_assert(std::is_trivially_copyable_v<MeshletBuildSettings>);
    static_assert(sizeof(MeshletBuildSettings) == 24 && alignof(MeshletBuildSettings) == 4);
    static_assert(std::is_trivially_copyable_v<MeshletDescriptor>);
    static_assert(std::is_standard_layout_v<MeshletDescriptor>);
    static_assert(sizeof(MeshletDescriptor) == 80 && alignof(MeshletDescriptor) == 16);
    static_assert(offsetof(MeshletDescriptor, flags) == 20);
    static_assert(offsetof(MeshletDescriptor, sphereCenter) == 32);
    static_assert(offsetof(MeshletDescriptor, sphereRadius) == 44);
    static_assert(offsetof(MeshletDescriptor, coneApex) == 48);
    static_assert(offsetof(MeshletDescriptor, coneAxis) == 64);
    static_assert(offsetof(MeshletDescriptor, coneCutoff) == 76);
    static_assert(sizeof(MeshletLodRange) == 8 && alignof(MeshletLodRange) == 4);
}
