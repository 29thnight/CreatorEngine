#pragma once

#include "../ModelData.h"

#include <string>

namespace experiment::importer
{
    // Call only after conversion, welding, tangent splitting, skin packing, and
    // cache/fetch reordering have finalized Mesh::vertices and Mesh::indices.
    // Replaces only outPayload; failure clears it and retains all indexed data.
    [[nodiscard]] bool BuildMeshlets(const Mesh& mesh, MeshletPayload& outPayload,
        std::string& diagnostic, const MeshletBuildSettings& settings = {});

    // A completely absent legacy payload is valid, but is not a meshlet route.
    // Any malformed/stale nonempty payload returns false with a diagnostic.
    // Readers may discard ONLY the derived payload and retain indexed geometry.
    [[nodiscard]] bool ValidateMeshlets(const Mesh& mesh, const MeshletPayload& payload,
        std::string& diagnostic, const MeshletBuildSettings& expectedSettings = {});

    // SHA-256 of the final packed geometry, layout and all builder settings.
    // Names, GUIDs, filesystem paths and modification times are deliberately absent.
    [[nodiscard]] MeshletGeometryDigest ComputeMeshletGeometryDigest(const Mesh& mesh,
        const MeshletBuildSettings& settings = {}) noexcept;
}
