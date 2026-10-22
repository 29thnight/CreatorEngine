#include "../../Utility_Framework/Ownership.h"
// Included beside full CPU geometry admission. No raw payload is fabricated.
own::shared_owner<const assets::ModelMeshDescriptor> DecodeModelStreamDescriptor(
    model_cooked::ResolvedAssetEntry resolved, own::shared_owner<const assets::ModelSkeletonPayload> skeleton,
    Error& error, std::string& failure, std::atomic<std::size_t>* inputStaging)
{
    own::shared_owner<const model_cooked::ModelGeometryStreamMetadata> stream;
    if (!model_cooked::LoadModelGeometryStreamMetadata(resolved, stream, failure, inputStaging))
    { error = Error::IntegrityFailed; return {}; }
    const bool skin = assets::Has(stream->attributes, assets::VertexAttribute::BoneIndices);
    if ((!skin && (!HasNoHardDependencies(resolved) || skeleton)) ||
        (skin && (!skeleton || !ValidateClipDependency(resolved, skeleton->origin) ||
                  stream->requiredBoneCount != skeleton->skeleton.bones.size() || stream->requiredSkinBindingSha256 != skeleton->skinBindingSha256)))
    { error = Error::DependencyFailed; failure = "Thin metadata and exact skin dependency differ."; return {}; }
    auto result = own::make_shared<assets::ModelMeshDescriptor>();
    result->origin = std::move(resolved); result->geometryKey = GeometryKey(result->origin);
    result->meshId = result->origin.entry.asset.key.assetId.value;
    result->vertexAttributeMask = stream->attributes; result->vertexStride = stream->stride;
    result->vertexLayoutHash = assets::VertexLayoutHash(stream->attributes); result->vertexCount = stream->vertexCount;
    result->indexCount = stream->counts[0].indexCount; result->bounds = stream->bounds; result->skinned = skin;
    result->requiredBoneCount = stream->requiredBoneCount; result->requiredSkinBindingSha256 = stream->requiredSkinBindingSha256;
    result->skeleton = std::move(skeleton);
    const auto summary = [&](const experiment::MeshletPayload& payload, uint32_t level) {
        auto item = MeshletSummary(payload); const auto& counts = stream->counts.at(level);
        item.vertexRemapCount = counts.vertexRemapCount; item.triangleIndexCount = counts.triangleByteCount;
        item.primitiveRemapCount = counts.primitiveRemapCount; return item;
    };
    result->meshlets = summary(stream->meshlets, 0); result->lodSettings = stream->lods.settings;
    result->lodGeometryDigest = stream->lods.geometryDigest; result->coarseLodCount = static_cast<uint32_t>(stream->lods.levels.size());
    for (uint32_t level = 0; level < result->coarseLodCount; ++level)
        result->coarseLods[level] = {stream->lods.levels[level].geometricError, stream->counts[level + 1].indexCount,
                                   summary(stream->lods.levels[level].meshlets, level + 1)};
    auto charge = AddModelCharge(sizeof(assets::ModelMeshDescriptor), ModelOriginDynamicCharge(result->origin));
    charge = AddModelCharge(charge, result->geometryKey.targetPlatform.capacity() + result->geometryKey.targetAbi.capacity());
    charge = AddModelCharge(charge, sizeof(model_cooked::ModelGeometryStreamMetadata) + MeshletDynamicCharge(stream->meshlets));
    charge = AddModelCharge(charge, stream->lods.levels.capacity() * sizeof(experiment::MeshLodLevel));
    for (const auto& level : stream->lods.levels) charge = AddModelCharge(charge, MeshletDynamicCharge(level.meshlets));
    charge = AddModelCharge(charge, stream->flatDescriptors->capacity() * sizeof(experiment::MeshletDescriptor));
    charge = AddModelCharge(charge, stream->counts.capacity() * sizeof(model_cooked::ModelStreamLodCounts) * 2);
    charge = AddModelCharge(charge, stream->lods.groups.capacity() * sizeof(experiment::MeshClusterGroup) * 2);
    charge = AddModelCharge(charge, sizeof(assets::cluster::Archive) + stream->archive->packets.capacity() * sizeof(std::vector<std::byte>));
    charge = AddModelCharge(charge, stream->archive->fileOffsets.capacity() * sizeof(uint64_t) * 2);
    charge = AddModelCharge(charge, stream->archive->packetSizes.capacity() * sizeof(uint32_t) * 2);
    charge = AddModelCharge(charge, stream->pageHashes.capacity() * sizeof(model_cooked::Sha256Digest) * 2);
    result->metadataBytes = charge; result->streamMetadata = std::move(stream); return result;
}
