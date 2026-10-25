#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../Utility_Framework/Foundation/StandardLibrary.h"

namespace material_graph
{
// Per-draw identity, not a cooked material dependency or GPU buffer handle.
// Geometry/LOD identifies the cooked cluster and primitive namespace;
// object/incarnation/instance identifies its placement; materialSlot identifies
// the sealed LX material instance. Both camera and shadow constants carry it.
struct alignas(16) SceneGeometryIdentity final
{
    uint64_t geometryKey{}, objectId{}, incarnation{}, instanceId{}, materialSlot{};
    // UINT32_MAX denotes a replacement hierarchy; actual LOD is carried by each cooked descriptor.
    uint32_t lodIndex{}, primitiveCount{}, version{1}, reserved[3]{};
};
static_assert(std::is_trivially_copyable_v<SceneGeometryIdentity>);
static_assert(sizeof(SceneGeometryIdentity) == 64);
static_assert(offsetof(SceneGeometryIdentity, materialSlot) == 32);
static_assert(offsetof(SceneGeometryIdentity, lodIndex) == 40);
} // namespace material_graph
