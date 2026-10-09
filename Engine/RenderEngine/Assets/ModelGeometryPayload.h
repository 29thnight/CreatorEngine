#pragma once

#include "ModelMeshDescriptor.h"

namespace assets
{
    // One compatible raw decode. The adapter's logical IDs/material/name stay
    // empty: different bound descriptors may share these immutable arrays while
    // retaining distinct skeleton closures and exact source backing.
    struct ModelGeometryPayload final
    {
        ModelGeometryKey key{};
        // Canonical verified content backing. This pair contains no logical
        // asset identity; each bound descriptor copies BOTH fields together.
        std::string artifactPath{};
        own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};
        ModelMeshAsset mesh{};
        ModelMeshletSummary meshletSummary{};
        std::array<ModelMeshLodSummary, experiment::kMeshLodMaxLevels> lodSummaries{};
        std::uint32_t requiredBoneCount{};
        experiment::cooked::Sha256Digest requiredSkinBindingSha256{};
        std::size_t decodedBytes{};

        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
        [[nodiscard]] bool Matches(const ModelMeshDescriptor& descriptor) const noexcept
        {
            if (key != descriptor.geometryKey || !mesh.meshId.IsNil() || !mesh.materialId.IsNil()
                || !mesh.name.empty() || mesh.bounds != descriptor.bounds
                || mesh.vertexAttributeMask != descriptor.vertexAttributeMask
                || !IsSupportedModelVertexLayout(mesh.vertexAttributeMask)
                || mesh.vertexStride != descriptor.vertexStride || mesh.vertexStride == 0u
                || mesh.vertexLayoutHash != descriptor.vertexLayoutHash
                || mesh.vertexLayoutHash != VertexLayoutHash(mesh.vertexAttributeMask)
                || mesh.vertexBytes.size() % mesh.vertexStride != 0u
                || mesh.vertexBytes.size() / mesh.vertexStride != descriptor.vertexCount
                || mesh.indices.size() != descriptor.indexCount
                || requiredBoneCount != descriptor.requiredBoneCount
                || requiredSkinBindingSha256 != descriptor.requiredSkinBindingSha256
                || meshletSummary != descriptor.meshlets || !meshletSummary.Matches(mesh.meshlets)
                || mesh.coarseLods.levels.size() != descriptor.coarseLodCount
                || mesh.coarseLods.levels.size() > lodSummaries.size()
                || mesh.coarseLods.geometryDigest != descriptor.lodGeometryDigest)
            {
                return false;
            }
            const auto& a = mesh.coarseLods.settings;
            const auto& b = descriptor.lodSettings;
            if (a.builderVersion != b.builderVersion || a.meshoptimizerVersion != b.meshoptimizerVersion
                || a.levelCount != b.levelCount || a.reductionRatio != b.reductionRatio
                || a.targetRelativeError != b.targetRelativeError || a.flags != b.flags)
            {
                return false;
            }
            for (std::size_t index = 0u; index < mesh.coarseLods.levels.size(); ++index)
            {
                const auto& lod = mesh.coarseLods.levels[index];
                if (lodSummaries[index] != descriptor.coarseLods[index]
                    || lodSummaries[index].geometricError != lod.geometricError
                    || lodSummaries[index].indexCount != lod.indices.size()
                    || !lodSummaries[index].meshlets.Matches(lod.meshlets))
                {
                    return false;
                }
            }
            const bool skinned = Has(mesh.vertexAttributeMask, VertexAttribute::BoneIndices);
            return skinned == descriptor.skinned
                && (skinned ? descriptor.skeleton
                    && descriptor.skeleton->skeleton.bones.size() == requiredBoneCount
                    && descriptor.skeleton->skinBindingSha256 == requiredSkinBindingSha256
                    : !descriptor.skeleton && requiredBoneCount == 0u
                        && requiredSkinBindingSha256 == experiment::cooked::Sha256Digest{});
        }
    };
}
