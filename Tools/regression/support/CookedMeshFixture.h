#pragma once

#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelGeometryPayload.h"
#include "Assets/ModelSkeletonPayload.h"
#include "Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "Experiment/Import/MeshletBuilder.h"
#include "Experiment/Import/MeshLodBuilder.h"
#include "RHI/IRenderDeviceServices.h"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

// METEOR Scene draws only granular geometry with current cooked meshlets. Test
// fixtures author meshes as loose vertex/index views; CookMeshFixture copies
// one such view into a fixture-owned payload, cooks its meshlets and returns a
// granular view over that payload. The source view's mesh id and model id name
// the fixture asset. Never a product asset path.
struct CookedMeshFixture
{
    own::shared_owner<assets::ModelMeshDescriptor> descriptor;
    own::shared_owner<const assets::ModelGeometryPayload> payload;
    RHIModelMeshView view{};
};

[[nodiscard]] inline bool CookMeshFixture(const RHIModelMeshView& source, CookedMeshFixture& result, std::string& error,
                                        const experiment::MeshletBuildSettings& settings = {},
                                        const experiment::MeshLodBuildSettings* lodSettings = nullptr)
{
    struct FixtureBytes final : experiment::cooked::ArtifactByteSource
    {
        bool Size(std::string_view, uint64_t&, std::string& why) const override
            { why = "Fixture geometry is already decoded"; return false; }
        bool ReadAt(std::string_view, uint64_t, std::span<std::byte>, std::string& why) const override
            { why = "Fixture geometry is already decoded"; return false; }
    };
    result = {};
    if (!source.vertexData || !source.indexData || !source.vertexStride || !source.indexCount ||
        source.vertexBytes % source.vertexStride != 0)
    {
        error = "Fixture mesh view is incomplete";
        return false;
    }
    const auto* bytes = static_cast<const std::byte*>(source.vertexData);
    experiment::Mesh authored;
    if (!authored.vertices.AssignPacked(source.vertexAttributeMask, source.vertexBytes / source.vertexStride,
        {bytes, static_cast<std::size_t>(source.vertexBytes)}))
    {
        error = "Fixture packed vertex layout rejected";
        return false;
    }
    authored.indices.assign(source.indexData, source.indexData + source.indexCount);
    if (!experiment::importer::BuildMeshlets(authored, authored.meshlets, error, settings))
    {
        error = "Fixture meshlet cooking failed: " + error;
        return false;
    }
    if (lodSettings && !experiment::importer::BuildMeshLods(authored, authored.coarseLods, error, *lodSettings, settings)) return false;
    auto payload = own::make_shared<assets::ModelGeometryPayload>();
    auto& mesh = payload->mesh;
    mesh.vertexAttributeMask = source.vertexAttributeMask;
    mesh.vertexStride = source.vertexStride;
    mesh.vertexLayoutHash = source.vertexLayoutHash;
    mesh.vertexBytes.assign(authored.vertices.Bytes().begin(), authored.vertices.Bytes().end());
    mesh.indices = std::move(authored.indices);
    mesh.meshlets = std::move(authored.meshlets);
    mesh.coarseLods = std::move(authored.coarseLods);
    auto& summary = payload->meshletSummary;
    summary.settings = mesh.meshlets.settings;
    summary.geometryDigest = mesh.meshlets.geometryDigest;
    summary.descriptorCount = static_cast<uint32_t>(mesh.meshlets.descriptors.size());
    summary.vertexRemapCount = static_cast<uint32_t>(mesh.meshlets.vertexRemap.size());
    summary.triangleIndexCount = static_cast<uint32_t>(mesh.meshlets.triangleIndices.size());
    summary.primitiveRemapCount = static_cast<uint32_t>(mesh.meshlets.primitiveRemap.size());
    summary.lod0 = mesh.meshlets.lod0;

    auto descriptorOwner = own::make_shared<assets::ModelMeshDescriptor>();
    auto& descriptor = *descriptorOwner;
    descriptor.meshId = source.handle.meshId;
    descriptor.vertexAttributeMask = mesh.vertexAttributeMask;
    descriptor.vertexStride = mesh.vertexStride;
    descriptor.vertexLayoutHash = mesh.vertexLayoutHash;
    descriptor.vertexCount = static_cast<uint32_t>(mesh.vertexBytes.size() / mesh.vertexStride);
    descriptor.indexCount = static_cast<uint32_t>(mesh.indices.size());
    descriptor.meshlets = summary;
    descriptor.lodSettings = mesh.coarseLods.settings;
    descriptor.lodGeometryDigest = mesh.coarseLods.geometryDigest;
    descriptor.coarseLodCount = static_cast<uint32_t>(mesh.coarseLods.levels.size());
    for (uint32_t i=0;i<descriptor.coarseLodCount;++i) {
        const auto& lod=mesh.coarseLods.levels[i]; auto& target=payload->lodSummaries[i];
        target.geometricError=lod.geometricError; target.indexCount=static_cast<uint32_t>(lod.indices.size());
        const auto& p=lod.meshlets;auto& m=target.meshlets;
        m.settings=p.settings;m.geometryDigest=p.geometryDigest;m.descriptorCount=static_cast<uint32_t>(p.descriptors.size());
        m.vertexRemapCount=static_cast<uint32_t>(p.vertexRemap.size());m.triangleIndexCount=static_cast<uint32_t>(p.triangleIndices.size());
        m.primitiveRemapCount=static_cast<uint32_t>(p.primitiveRemap.size());m.lod0=p.lod0;
        descriptor.coarseLods[i]=target;
    }
    descriptor.skinned = assets::Has(mesh.vertexAttributeMask, assets::VertexAttribute::BoneIndices);
    if (descriptor.skinned)
    {
        const auto boneOffset = assets::OffsetOf(mesh.vertexAttributeMask, assets::VertexAttribute::BoneIndices);
        uint32_t bones = 1;
        for (uint32_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
            for (uint32_t lane = 0; lane < 4; ++lane)
                bones = std::max(bones, 1u + uint32_t(std::to_integer<uint8_t>(
                    mesh.vertexBytes[vertex * mesh.vertexStride + boneOffset + lane])));
        auto skeleton = own::make_shared<assets::ModelSkeletonStorage>();
        skeleton->skeleton.bones.resize(bones);
        descriptor.skeleton = own::make_shared<assets::ModelSkeletonPayload>(
            std::move(skeleton), experiment::cooked::ResolvedAssetEntry{}, 0);
        descriptor.requiredBoneCount = payload->requiredBoneCount = bones;
    }
    auto& origin = descriptor.origin;
    origin.entry.asset.key.assetId.value = descriptor.meshId;
    origin.entry.asset.kind = experiment::cooked::CookedAssetKind::Mesh;
    origin.blob.kind = experiment::cooked::CookedAssetKind::Mesh;
    origin.blob.contentSha256 = mesh.meshlets.geometryDigest;
    origin.blob.byteSize = mesh.vertexBytes.size() + mesh.indices.size() * 4;
    origin.blob.representation = 1;
    origin.blob.schemaVersion = experiment::cooked::kModelGeometryArtifactVersion;
    origin.byteSource = own::make_shared<FixtureBytes>();
    origin.resolverRevision = origin.mountId.value = origin.manifestRevision = 1;
    origin.assetSetId.value = source.handle.modelId;
    descriptor.geometryKey.contentSha256 = origin.blob.contentSha256;
    descriptor.geometryKey.byteSize = origin.blob.byteSize;
    descriptor.geometryKey.kind = origin.blob.kind;
    descriptor.geometryKey.representation = origin.blob.representation;
    descriptor.geometryKey.schemaVersion = origin.blob.schemaVersion;
    payload->key = descriptor.geometryKey;
    result.payload = std::move(payload);
    if (!BuildRHIModelMeshView(descriptor, *result.payload, result.view))
    {
        result = {};
        error = "Fixture cooked typed geometry rejected";
        return false;
    }
    result.descriptor = std::move(descriptorOwner);
    return true;
}

// Keeps every mesh a probe cooked alive for the probe's lifetime and answers
// the Scene seal's pin tables for draws that reference them.
class CookedMeshLibrary
{
public:
    [[nodiscard]] bool Cook(const RHIModelMeshView& loose, RHIModelMeshView& cooked, std::string& error,
                            const experiment::MeshletBuildSettings& settings = {},
                            const experiment::MeshLodBuildSettings* lodSettings = nullptr)
    {
        auto& mesh = m_meshes.emplace_back();
        if (!CookMeshFixture(loose, mesh, error, settings, lodSettings))
        {
            m_meshes.pop_back();
            return false;
        }
        cooked = mesh.view;
        return true;
    }

    // Returns a granular view for a complete loose view, cooking it once per
    // distinct content so repeated frames keep one geometry identity. Granular
    // and incomplete views pass through unchanged for the seal to judge.
    // view is a copy so resolved may alias the caller's source.
    [[nodiscard]] bool Resolve(const RHIModelMeshView view, RHIModelMeshView& resolved, std::string& error,
                               const experiment::MeshletBuildSettings& settings = {})
    {
        resolved = view;
        if (view.handle.domain == assets::ModelMeshDomain::Granular || !view.IsComplete())
            return true;
        const auto* bytes = static_cast<const std::byte*>(view.vertexData);
        for (const auto& entry : m_loose)
        {
            const auto& cookedSettings = m_meshes[entry.mesh].payload->mesh.meshlets.settings;
            if (cookedSettings.profileVersion != settings.profileVersion || cookedSettings.builderVersion != settings.builderVersion ||
                cookedSettings.meshoptimizerVersion != settings.meshoptimizerVersion || cookedSettings.maxVertices != settings.maxVertices ||
                cookedSettings.maxTriangles != settings.maxTriangles || cookedSettings.coneWeight != settings.coneWeight) continue;
            if (entry.meshId == view.handle.meshId && entry.modelId == view.handle.modelId &&
                entry.mask == view.vertexAttributeMask &&
                std::ranges::equal(entry.vertices, std::span(bytes, static_cast<std::size_t>(view.vertexBytes))) &&
                std::ranges::equal(entry.indices, std::span(view.indexData, view.indexCount)))
            {
                resolved = m_meshes[entry.mesh].view;
                return true;
            }
        }
        if (!Cook(view, resolved, error, settings))
            return false;
        m_loose.push_back({view.handle.meshId, view.handle.modelId, view.vertexAttributeMask,
            {bytes, bytes + view.vertexBytes}, {view.indexData, view.indexData + view.indexCount},
            m_meshes.size() - 1});
        return true;
    }

    // Pins each cooked mesh that a draw references, once. Loose and foreign
    // views are left out, so the seal still rejects them.
    template <class Draws>
    void Pin(const Draws& draws, assets::ModelAssetGenerationPins& descriptors,
        assets::ModelGeometryPreparationPins& geometry) const
    {
        for (const auto& draw : draws)
        {
            const auto* descriptor = draw.modelMeshView.sourceDescriptor;
            if (!descriptor || std::ranges::any_of(descriptors.meshes,
                    [&](const auto& pinned) { return pinned && &*pinned == descriptor; }))
                continue;
            const auto mesh = std::ranges::find_if(m_meshes,
                [&](const CookedMeshFixture& cooked) { return cooked.descriptor && &*cooked.descriptor == descriptor; });
            if (mesh == m_meshes.end())
                continue;
            descriptors.meshes.push_back(mesh->descriptor);
            geometry.entries.push_back({mesh->view.handle, mesh->payload});
        }
    }

private:
    struct LooseSource
    {
        decltype(assets::ModelMeshHandle{}.meshId) meshId;
        decltype(assets::ModelMeshHandle{}.modelId) modelId;
        uint32_t mask{};
        std::vector<std::byte> vertices;
        std::vector<uint32_t> indices;
        std::size_t mesh{};
    };
    std::deque<CookedMeshFixture> m_meshes;
    std::vector<LooseSource> m_loose;
};
