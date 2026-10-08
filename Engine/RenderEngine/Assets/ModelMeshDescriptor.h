#pragma once

#include "ModelSkeletonPayload.h"
#include "../Experiment/Cooked/CookedModelSubAssetCodec.h"

#include <array>
#include <limits>

namespace assets
{
    // Compatible raw decode identity. Logical IDs, source paths and resolver
    // revisions deliberately do not participate in geometry sharing.
    struct ModelGeometryKey final
    {
        experiment::cooked::Sha256Digest contentSha256{};
        std::uint64_t byteSize{};
        experiment::cooked::CookedAssetKind kind{ experiment::cooked::CookedAssetKind::Mesh };
        std::uint32_t representation{};
        std::uint32_t schemaVersion{};
        std::string targetPlatform{};
        std::string targetAbi{};
        // Increment when native layout or output-changing decoder policy changes.
        std::uint32_t decoderRecipe{ 1u };
        friend auto operator<=>(const ModelGeometryKey&, const ModelGeometryKey&) = default;
    };

    struct ModelMeshletSummary final
    {
        experiment::MeshletBuildSettings settings{};
        experiment::MeshletGeometryDigest geometryDigest{};
        std::uint32_t descriptorCount{};
        std::uint32_t vertexRemapCount{};
        std::uint32_t triangleIndexCount{};
        std::uint32_t primitiveRemapCount{};
        experiment::MeshletLodRange lod0{};
        math::vector4 localBoundsSphere{};
        friend bool operator==(const ModelMeshletSummary& a, const ModelMeshletSummary& b) noexcept
        {
            return a.settings.profileVersion == b.settings.profileVersion
                && a.settings.builderVersion == b.settings.builderVersion
                && a.settings.meshoptimizerVersion == b.settings.meshoptimizerVersion
                && a.settings.maxVertices == b.settings.maxVertices
                && a.settings.maxTriangles == b.settings.maxTriangles
                && a.settings.coneWeight == b.settings.coneWeight
                && a.geometryDigest == b.geometryDigest && a.descriptorCount == b.descriptorCount
                && a.vertexRemapCount == b.vertexRemapCount && a.triangleIndexCount == b.triangleIndexCount
                && a.primitiveRemapCount == b.primitiveRemapCount && a.lod0.firstMeshlet == b.lod0.firstMeshlet
                && a.lod0.meshletCount == b.lod0.meshletCount && a.localBoundsSphere == b.localBoundsSphere;
        }
        [[nodiscard]] bool Matches(const experiment::MeshletPayload& payload) const noexcept
        {
            return settings.profileVersion == payload.settings.profileVersion
                && settings.builderVersion == payload.settings.builderVersion
                && settings.meshoptimizerVersion == payload.settings.meshoptimizerVersion
                && settings.maxVertices == payload.settings.maxVertices
                && settings.maxTriangles == payload.settings.maxTriangles
                && settings.coneWeight == payload.settings.coneWeight
                && geometryDigest == payload.geometryDigest
                && descriptorCount == payload.descriptors.size()
                && vertexRemapCount == payload.vertexRemap.size()
                && triangleIndexCount == payload.triangleIndices.size()
                && primitiveRemapCount == payload.primitiveRemap.size()
                && lod0.firstMeshlet == payload.lod0.firstMeshlet
                && lod0.meshletCount == payload.lod0.meshletCount;
        }
        [[nodiscard]] bool HasMeshlets() const noexcept { return descriptorCount != 0u; }
    };

    struct ModelMeshLodSummary final
    {
        float geometricError{};
        std::uint32_t indexCount{};
        ModelMeshletSummary meshlets{};
        friend bool operator==(const ModelMeshLodSummary&, const ModelMeshLodSummary&) noexcept = default;
    };

    // Immutable, independently useful render metadata. Reproducible vertex,
    // index, meshlet and LOD arrays are NEVER owned by this descriptor.
    struct ModelMeshDescriptor final
    {
        // entry/revisions identify this logical generation and hard closure.
        // blob.artifactPath + byteSource are a matched, verified exact backing
        // pair, which may be shared with compatible content from another mount.
        experiment::cooked::ResolvedAssetEntry origin{};
        ModelGeometryKey geometryKey{};
        Uuid::Uuid16 meshId{};
        // Names and material selection are model-summary/renderer metadata,
        // not part of independently shared geometry or its hard dependency.
        std::uint32_t vertexAttributeMask{};
        std::uint32_t vertexStride{};
        std::uint64_t vertexLayoutHash{};
        std::uint32_t vertexCount{};
        std::uint32_t indexCount{};
        math::aabb bounds{};
        bool skinned{};
        std::uint32_t requiredBoneCount{};
        experiment::cooked::Sha256Digest requiredSkinBindingSha256{};
        own::shared_owner<const ModelSkeletonPayload> skeleton{};
        ModelMeshletSummary meshlets{};
        experiment::MeshLodBuildSettings lodSettings{};
        experiment::MeshletGeometryDigest lodGeometryDigest{};
        std::array<ModelMeshLodSummary, experiment::kMeshLodMaxLevels> coarseLods{};
        std::uint32_t coarseLodCount{};
        std::size_t metadataBytes{};

        // Cache retention conservatively charges the hard closure, even when
        // another descriptor shares the same skeleton. metadataBytes remains
        // the independently reportable small descriptor allocation estimate.
        [[nodiscard]] std::size_t ByteSize() const noexcept
        {
            const auto dependency = skeleton ? skeleton->ByteSize() : 0u;
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            return dependency > maximum - metadataBytes ? maximum : metadataBytes + dependency;
        }
    };
}
