#pragma once

#include "../MeshLodData.h"
#include "../ModelData.h"

#include <string>

namespace experiment::importer
{
    // Authoring only, after base vertex/index finalization. Creates separate
    // coarse index arrays and per-level meshlets; never changes LOD0 geometry.
    // Disabled/skinned/non-reducible meshes succeed with an empty chain and an
    // explicit diagnostic. Failure clears outLods and returns false.
    [[nodiscard]] bool BuildMeshLods(const Mesh& mesh, MeshLodChain& outLods,
        std::string& diagnostic, const MeshLodBuildSettings& settings = {},
        const MeshletBuildSettings& meshletSettings = {});

    // Absent legacy data is valid. Nonempty data must match finalized LOD0,
    // contain strictly decreasing triangle counts and valid per-level meshlets,
    // and reference only vertices used by the original base index stream.
    // A caller may discard the entire derived chain and retain valid LOD0.
    [[nodiscard]] bool ValidateMeshLods(const Mesh& mesh, const MeshLodChain& lods,
        std::string& diagnostic);

    // Binds settings and all coarse topology/error/meshlet bytes to unchanged
    // finalized base geometry. No source paths, names or runtime buffer handles.
    [[nodiscard]] MeshletGeometryDigest ComputeMeshLodGeometryDigest(
        const Mesh& mesh, const MeshLodChain& lods) noexcept;
}
