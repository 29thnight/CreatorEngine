#include "CookedAssetCatalog.h"
#include "CookedMaterialProgram.h"
#include "CookedAudioClipSource.h"
#include "../../Assets/AssetIdentityProfile.h"
#include "../../MaterialGraphShaderMeta.h"

#include <algorithm>
#include <cstring>

namespace experiment::cooked
{
static_assert(kMaterialProgramArtifactVersion == material_graph::CookedProgramVersion);

namespace
{
bool MaterialTextureDependencies(const LX::LXMaterialProgram& program, std::vector<AssetId>& result, std::string& error)
{
    for (const auto& resource : program.resources)
    {
        if (resource.kind != LX::LXMaterialResourceKind::Texture)
            continue;
        AssetId id;
        if (!TryParseCanonicalAssetId(resource.reference, id) &&
            !assets::TryParseCanonicalUuidV8(resource.reference, id.value))
        {
            error = "Material texture reference must be a canonical asset UUID: " + resource.reference;
            return false;
        }
        if (std::ranges::find(result, id) == result.end())
            result.push_back(id);
    }
    std::ranges::sort(result);
    return true;
}
} // namespace

bool BuildMaterialProgramCookProduct(const AssetId& graphId, const LX::LXMaterialAsset& graph,
                                     const material_graph::VerifiedProduct& verified,
                                     const material_graph::Budget& budget, MaterialProgramCookProduct& result,
                                     std::string& error)
{
    MaterialProgramCookProduct candidate;
    candidate.manifestEntry.artifactPath = MakeDerivedMaterialProgramArtifactPath(graphId);
    if (candidate.manifestEntry.artifactPath.empty())
    {
        error = "Material graph identity must be a canonical UUIDv4 or model UUIDv8.";
        return false;
    }
    const auto generated = LX::GenerateMaterialSlang(graph);
    if (verified.materialShader && verified.materialShader->meta.guid.m_guid != graphId.value)
    {
        error = "Generated material metadata belongs to a different graph identity.";
        return false;
    }
    if (!generated || generated->slang != verified.program.slang ||
        LX::WriteMaterialProgramMetadata(*generated) != LX::WriteMaterialProgramMetadata(verified.program))
    {
        error = "Verified material specialization differs from the current source graph; regenerate before cook.";
        return false;
    }
    if (!MaterialTextureDependencies(verified.program, candidate.manifestEntry.dependencies, error))
        return false;
    std::vector<std::uint8_t> payload;
    if (!material_graph::WriteCookedProgram(verified, budget, payload, error))
        return false;
    candidate.artifactBytes.resize(payload.size());
    std::memcpy(candidate.artifactBytes.data(), payload.data(), payload.size());
    candidate.manifestEntry.assetId = graphId;
    candidate.manifestEntry.kind = CookedAssetKind::MaterialProgram;
    candidate.manifestEntry.formatVersion = kMaterialProgramArtifactVersion;
    candidate.manifestEntry.byteSize = payload.size();
    if (!ComputeSha256(candidate.artifactBytes, candidate.manifestEntry.contentSha256, error))
        return false;
    result = std::move(candidate);
    error.clear();
    return true;
}

bool OpenCookedMaterialProgram(const CookedAssetManifestEntry& entry, const ArtifactByteSource& bytes,
                               const material_graph::Budget& budget, material_graph::CookedProgram& result,
                               std::string& error)
{
    const auto invalid = [&]() {
        error = "Material program manifest identity, size, hash or dependencies are invalid.";
        return false;
    };
    if ((!IsAssetIdV4(entry.assetId) && !assets::IsUuidV8(entry.assetId.value)) || budget.compiledBytes > UINT32_MAX ||
        entry.kind != CookedAssetKind::MaterialProgram || entry.formatVersion != kMaterialProgramArtifactVersion ||
        entry.byteSize < 20 || entry.byteSize > budget.compiledBytes + 128ull * 1024ull * 1024ull ||
        entry.artifactPath != MakeDerivedMaterialProgramArtifactPath(entry.assetId))
        return invalid();
    std::uint64_t size{};
    if (!bytes.Size(entry.artifactPath, size, error))
        return false;
    if (size != entry.byteSize)
        return invalid();
    std::vector<std::byte> payload(static_cast<std::size_t>(size));
    if (!bytes.ReadAt(entry.artifactPath, 0, payload, error))
        return false;
    Sha256Digest digest;
    if (!ComputeSha256(payload, digest, error))
        return false;
    if (digest != entry.contentSha256)
        return invalid();
    material_graph::CookedProgram candidate;
    if (!material_graph::ReadCookedProgram({reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()},
                                           budget, candidate, error))
        return false;
    if (candidate.product.materialShader && candidate.product.materialShader->meta.guid.m_guid != entry.assetId.value)
        return invalid();
    std::vector<AssetId> dependencies;
    if (!MaterialTextureDependencies(candidate.product.program, dependencies, error))
        return false;
    auto recorded = entry.dependencies;
    std::ranges::sort(recorded);
    if (dependencies != recorded)
        return invalid();
    result = std::move(candidate);
    error.clear();
    return true;
}
} // namespace experiment::cooked

namespace experiment::cooked
{
    bool CookedAssetCatalog::OpenMaterialProgram(const AssetId& assetId,
        const ArtifactByteSource& bytes, const material_graph::Budget& budget,
        material_graph::CookedProgram& out, std::string& failure) const
    {
        const auto* entry = Find(assetId);
        if (!entry || entry->kind != CookedAssetKind::MaterialProgram)
        {
            failure = "Material graph has no cooked program in the catalog.";
            return false;
        }
        for (const auto& dependency : entry->dependencies)
        {
            const auto* texture = Find(dependency);
            if (!texture || texture->kind != CookedAssetKind::Texture)
            {
                failure = "Cooked material texture dependency is missing or has the wrong kind.";
                return false;
            }
        }
        return OpenCookedMaterialProgram(*entry, bytes, budget, out, failure);
    }

}
