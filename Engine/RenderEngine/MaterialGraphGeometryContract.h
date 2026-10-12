#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace material_graph
{
    // Per-draw identity, not a cooked material dependency or GPU buffer handle.
    // Geometry/LOD identifies the cooked cluster and primitive namespace;
    // object/incarnation/instance identifies its placement; materialSlot identifies
    // the sealed LX material instance. Both camera and shadow constants carry it.
    struct alignas(16) SceneGeometryIdentity final
    {
        uint64_t geometryKey{}, objectId{}, incarnation{}, instanceId{}, materialSlot{};
        uint32_t lodIndex{}, primitiveCount{}, version{1}, reserved[3]{};
    };
    static_assert(std::is_trivially_copyable_v<SceneGeometryIdentity>);
    static_assert(sizeof(SceneGeometryIdentity) == 64);
    static_assert(offsetof(SceneGeometryIdentity, materialSlot) == 32);
    static_assert(offsetof(SceneGeometryIdentity, lodIndex) == 40);
}
