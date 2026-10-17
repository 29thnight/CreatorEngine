#pragma once

#include "../MeshLodData.h"
#include "../ModelData.h"

#include "../../../Utility_Framework/StandardLibrary.h"

namespace experiment::importer
{
// Authoring only, after base vertex/index finalization. Creates separate
// spatial replacement patches, aggregated by depth; never changes LOD0 geometry.
// Disabled/skinned/single-cluster meshes succeed with an empty chain and an
// explicit diagnostic. Failure clears outLods and returns false.
[[nodiscard]] bool BuildMeshLods(const Mesh& mesh, MeshLodChain& outLods, std::string& diagnostic,
                                 const MeshLodBuildSettings& settings = {},
                                 const MeshletBuildSettings& meshletSettings = {});

// A single-cluster/disabled mesh has an empty chain. Nonempty
// data must carry the current cooked replacement hierarchy and match LOD0,
// preserve patch boundary edges, disjoint cluster coverage and valid per-depth meshlets,
// and reference only vertices used by the original base index stream.
// Malformed/stale chains require reimport/recook; never downgrade the scene.
[[nodiscard]] bool ValidateMeshLods(const Mesh& mesh, const MeshLodChain& lods, std::string& diagnostic);

// Binds settings and all coarse topology/error/meshlet bytes to unchanged
// finalized base geometry. No source paths, names or runtime buffer handles.
[[nodiscard]] MeshletGeometryDigest ComputeMeshLodGeometryDigest(const Mesh& mesh, const MeshLodChain& lods) noexcept;
} // namespace experiment::importer
