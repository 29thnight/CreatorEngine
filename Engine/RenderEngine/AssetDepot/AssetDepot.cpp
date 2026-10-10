#include "../DataSystem.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"
#include "../Experiment/Cooked/CookedTexture.h"
#include "AssetSetActivation.h"
#include "../Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "../Experiment/Cooked/CookedShaderMeta.h"
#include "../Experiment/Cooked/MaterialAssetSetCodec.h"
#include "../Experiment/Cooked/CookedCodeMaterial.h"
#include "../Experiment/Cooked/CookedInputGraph.h"


#include <limits>
#include <utility>
#include "../../Utility_Framework/ContentAbi.h"

bool DataSystem::PublishAuthoredTexture(const RuntimeAssetChange& change)
{
    namespace cooked = experiment::cooked;
    const auto& publication = change.texturePublication;
    if (!publication || !publication->latestRevision
        || publication->latestRevision->load(std::memory_order_acquire) != publication->revision)
    {
        if (publication)
        {
            publication->state.store(RuntimeTexturePublication::State::Superseded, std::memory_order_release);
        }
        return false;
    }
    if (publication->manifest.entries.size() != 1u || publication->manifest.blobs.size() != 1u
        || publication->manifest.entries.front().asset.key.assetId != experiment::AssetId{ change.guid.m_guid }
        || publication->manifest.entries.front().asset.kind != cooked::CookedAssetKind::Texture)
    {
        publication->state.store(RuntimeTexturePublication::State::Failed, std::memory_order_release);
        return false;
    }
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            publication->state.store(RuntimeTexturePublication::State::Failed, std::memory_order_release);
            return false;
        }
        if (m_assetRootHandoffs != 0u || m_assetInvalidationDepth != 0u)
        {
            QueueAssetChange(change);
            return false;
        }
    }
    if (!ApplyAssetChange({ RuntimeAssetChangeKind::CatalogUpsert,
        RuntimeAssetType::Texture, change.guid, change.path }))
    {
        publication->state.store(RuntimeTexturePublication::State::Failed, std::memory_order_release);
        return false;
    }
    cooked::AssetSetMountOptions options;
    options.expectedTargetPlatform = "win-x64";
    options.expectedTargetAbi = CreatorContentAbi::Token;
    const auto reference = publication->manifest.entries.front().asset;
    const auto catalog = GetCookedCatalog();
    cooked::ResolvedAssetEntry previous;
    const bool hasPrevious = catalog
        && catalog->Find(reference, previous) == cooked::AssetLookupStatus::Found;
    auto comparableBlob = previous.blob;
    // Locators belong to their immutable mount backing, not typed byte identity.
    // Match CookedAssetCatalog's identical-definition test across package/editor paths.
    comparableBlob.artifactPath = publication->manifest.blobs.front().artifactPath;
    const bool identical = hasPrevious
        && comparableBlob == publication->manifest.blobs.front()
        && previous.entry.dependencies == publication->manifest.entries.front().dependencies;
    if (!identical)
    {
        if (hasPrevious)
        {
            options.overrideIdentities.push_back(reference.key);
        }
        std::vector<cooked::AssetManifestIssue> issues;
        AssetDepot::AssetMountId previousMount;
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (const auto found = m_authoredTextureMounts.find(change.guid); found != m_authoredTextureMounts.end())
            {
                previousMount = found->second;
            }
        }
        const auto mounted = MountAssetSet(publication->manifestBytes, publication->byteSource,
            options, issues, previousMount);
        if (!mounted.IsValid())
        {
            for (const auto& issue : issues)
            {
                if (issue.context == "mount" && issue.message.find("stale") != std::string::npos)
                {
                    QueueAssetChange(change);
                    return false;
                }
                Debug::PrintLog(spdlog::level::err,
                    "Texture generation publication failed [" + issue.context + "]: " + issue.message);
            }
            publication->state.store(RuntimeTexturePublication::State::Failed, std::memory_order_release);
            return false;
        }
        {
            std::lock_guard lock(m_assetPreparationMutex);
            m_authoredTextureMounts[change.guid] = mounted;
        }
    }
    publication->request = RequestAsync<Texture>(AssetDepot::AssetLink<Texture>{ reference.key });
    publication->state.store(RuntimeTexturePublication::State::Loading, std::memory_order_release);
    return true;
}

namespace AssetDepot
{
    bool ValidateAssetSetRuntimeCompatibility(const experiment::cooked::AssetSetManifest& manifest,
        std::vector<experiment::cooked::AssetManifestIssue>& issues)
    {
        namespace cooked = experiment::cooked;
        issues.clear();
        for (const auto& blob : manifest.blobs)
        {
            std::uint32_t representation{};
            std::uint32_t schema{};
            switch (blob.kind)
            {
            case cooked::CookedAssetKind::InputGraph:
                representation = cooked::kInputGraphRepresentation;
                schema = cooked::kInputGraphArtifactVersion;
                break;
            case cooked::CookedAssetKind::Texture:
                representation = cooked::kCookedTextureRepresentationVersion;
                schema = cooked::kTextureArtifactVersion;
                break;
            case cooked::CookedAssetKind::Model:
                representation = cooked::kModelDescriptorRepresentation;
                schema = cooked::kModelDescriptorVersion;
                break;
            case cooked::CookedAssetKind::Mesh:
                representation = cooked::kModelGeometryRepresentation;
                schema = cooked::kModelGeometryArtifactVersion;
                break;
            case cooked::CookedAssetKind::Skeleton:
                representation = cooked::kSkeletonRepresentation;
                schema = cooked::kSkeletonArtifactVersion;
                break;
            case cooked::CookedAssetKind::AnimationClip:
                representation = cooked::kAnimationClipRepresentation;
                schema = cooked::kAnimationClipArtifactVersion;
                break;
            case cooked::CookedAssetKind::ShaderMeta:
                representation = cooked::kShaderMetaDocumentRepresentation;
                schema = cooked::kShaderMetaDocumentVersion;
                break;
            case cooked::CookedAssetKind::Material:
                representation = blob.representation == cooked::kAuthoredMaterialRepresentation
                    ? cooked::kAuthoredMaterialRepresentation : cooked::kMaterialDocumentRepresentation;
                schema = blob.representation == cooked::kAuthoredMaterialRepresentation
                    ? cooked::kAuthoredMaterialVersion : cooked::kMaterialArtifactVersion;
                break;
            case cooked::CookedAssetKind::MaterialProgram:
                representation = blob.representation == cooked::kCodeProgramRepresentation
                    ? cooked::kCodeProgramRepresentation : cooked::kMaterialProgramRepresentation;
                schema = blob.representation == cooked::kCodeProgramRepresentation
                    ? cooked::kCodeProgramVersion : cooked::kMaterialProgramArtifactVersion;
                break;
            default:
                break;
            }
            if (representation == 0u || blob.representation != representation || blob.schemaVersion != schema)
            {
                issues.push_back({ "mount.compatibility." + blob.artifactPath,
                    "No installed CPU asset decoder for kind=" + std::to_string(static_cast<unsigned>(blob.kind))
                    + " representation=" + std::to_string(blob.representation)
                    + " schema=" + std::to_string(blob.schemaVersion) + "; recook for this Player." });
                return false;
            }
        }
        return true;
    }
}

AssetDepot::AssetMountId DataSystem::MountAssetSet(
    std::span<const std::byte> manifestBytes,
    own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource,
    const experiment::cooked::AssetSetMountOptions& options,
    std::vector<experiment::cooked::AssetManifestIssue>& outIssues,
    AssetDepot::AssetMountId replaceMount)
{
    namespace cooked = experiment::cooked;
    outIssues.clear();
    cooked::AssetSetManifest manifest;
    if (!cooked::ReadAssetSetManifest(manifestBytes, manifest, outIssues)
        || !AssetDepot::ValidateAssetSetRuntimeCompatibility(manifest, outIssues))
    {
        return {};
    }

    own::shared_owner<const cooked::CookedAssetCatalog> snapshot;
    AssetDepot::AssetMountId mountId;
    std::uint64_t revision{};
    std::uint64_t epoch{};
    {
        // Use the same admission order as asynchronous asset work. Candidate
        // validation and storage I/O never run under these locks.
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetRootHandoffs != 0u)
        {
            outIssues.push_back({ "mount", "asset admission has stopped" });
            return {};
        }
        if (m_assetDepotRevision > (std::numeric_limits<std::uint64_t>::max)() - (replaceMount.IsValid() ? 2u : 1u)
            || m_nextAssetMountId == (std::numeric_limits<std::uint64_t>::max)())
        {
            outIssues.push_back({ "mount", "asset mount identity/revision space exhausted" });
            return {};
        }
        snapshot = m_cookedCatalog;
        revision = m_assetDepotRevision;
        epoch = m_assetPreparationEpoch;
        // Reserved identities are never reused, including failed transactions
        // and reinitialization of the same DataSystem lifecycle owner.
        mountId.value = m_nextAssetMountId++;
    }

    const cooked::CookedAssetCatalog empty;
    const cooked::CookedAssetCatalog& current = snapshot ? *snapshot : empty;
    cooked::CookedAssetCatalog candidate;
    if (!current.WithMountedAssetSet(manifest, std::move(byteSource), mountId,
        revision + 1u, options, candidate, outIssues))
    {
        return {};
    }
    const auto publishedRevision = revision + (replaceMount.IsValid() ? 2u : 1u);
    if (replaceMount.IsValid())
    {
        cooked::CookedAssetCatalog replacement;
        if (!candidate.WithoutMountedAssetSet(replaceMount, publishedRevision, replacement, outIssues))
        {
            return {};
        }
        candidate = std::move(replacement);
    }
    // No intermediate resolver exposes both the replacement and its retired
    // logical definition. Old descriptor/source owners pin backing independently.
    auto published = own::make_shared<const cooked::CookedAssetCatalog>(std::move(candidate));
    own::shared_owner<const cooked::CookedAssetCatalog> retired;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    AssetDepot::MaterialAssetRetiredEntries retiredMaterials;
    LegacyCacheRetirement retiredLegacy;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetRootHandoffs != 0u || m_assetPreparationEpoch != epoch
            || m_assetDepotRevision != revision)
        {
            outIssues.push_back({ "mount", "resolver changed before publication; mount is stale" });
            return {};
        }
        StageLegacyCacheRetirementLocked(retiredLegacy);
        StageTextureAssetRetirementLocked(retiredTextures);
        StageModelAssetRetirementLocked(retiredModels);
        StageMaterialAssetRetirementLocked(retiredMaterials);
        retired = std::move(m_cookedCatalog);
        m_cookedCatalog = std::move(published);
        m_assetDepotRevision = publishedRevision;
        DetachLegacyCachesLocked(retiredLegacy);
        InvalidateTextureAssetsLocked(retiredTextures);
        InvalidateModelAssetsLocked(retiredModels);
        InvalidateMaterialAssetsLocked(retiredMaterials);
    }
    return mountId;
}

bool DataSystem::UnmountAssetSet(AssetDepot::AssetMountId mountId,
    std::vector<experiment::cooked::AssetManifestIssue>& outIssues)
{
    namespace cooked = experiment::cooked;
    outIssues.clear();
    own::shared_owner<const cooked::CookedAssetCatalog> snapshot;
    std::uint64_t revision{};
    std::uint64_t epoch{};
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetRootHandoffs != 0u || !m_cookedCatalog)
        {
            outIssues.push_back({ "unmount", "asset set is not mounted or admission has stopped" });
            return false;
        }
        if (m_assetDepotRevision == (std::numeric_limits<std::uint64_t>::max)())
        {
            outIssues.push_back({ "unmount", "resolver revision space exhausted" });
            return false;
        }
        snapshot = m_cookedCatalog;
        revision = m_assetDepotRevision;
        epoch = m_assetPreparationEpoch;
    }
    cooked::CookedAssetCatalog candidate;
    if (!snapshot->WithoutMountedAssetSet(mountId, revision + 1u, candidate, outIssues))
    {
        return false;
    }
    auto published = own::make_shared<const cooked::CookedAssetCatalog>(std::move(candidate));
    own::shared_owner<const cooked::CookedAssetCatalog> retired;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    AssetDepot::MaterialAssetRetiredEntries retiredMaterials;
    LegacyCacheRetirement retiredLegacy;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetRootHandoffs != 0u || m_assetPreparationEpoch != epoch
            || m_assetDepotRevision != revision)
        {
            outIssues.push_back({ "unmount", "resolver changed before publication; unmount is stale" });
            return false;
        }
        StageLegacyCacheRetirementLocked(retiredLegacy);
        StageTextureAssetRetirementLocked(retiredTextures);
        StageModelAssetRetirementLocked(retiredModels);
        StageMaterialAssetRetirementLocked(retiredMaterials);
        retired = std::move(m_cookedCatalog);
        m_cookedCatalog = std::move(published);
        m_assetDepotRevision = revision + 1u;
        DetachLegacyCachesLocked(retiredLegacy);
        InvalidateTextureAssetsLocked(retiredTextures);
        InvalidateModelAssetsLocked(retiredModels);
        InvalidateMaterialAssetsLocked(retiredMaterials);
    }
    // Existing snapshots and resolved generation owners keep the exact backing.
    // No storage deletion or GPU retirement is implied by logical unmount.
    return true;
}

std::vector<experiment::cooked::TypedAssetReference> DataSystem::ListAssetSetRoots(
    AssetDepot::AssetMountId mountId, experiment::cooked::CookedAssetKind kind) const
{
    own::shared_owner<const experiment::cooked::CookedAssetCatalog> snapshot;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
        {
            return {};
        }
        snapshot = m_cookedCatalog;
    }
    return snapshot ? snapshot->ListRoots(mountId, kind)
        : std::vector<experiment::cooked::TypedAssetReference>{};
}


bool DataSystem::PublishAuthoredInputGraph(const RuntimeAssetChange& change)
{
    namespace cooked = experiment::cooked;
    const auto& publication = change.inputGraphPublication;
    if (!publication || !publication->latestRevision ||
        publication->latestRevision->load(std::memory_order_acquire) != publication->revision ||
        publication->manifest.entries.size() != 1u ||
        publication->manifest.blobs.size() != 1u ||
        publication->manifest.entries.front().asset.key.assetId != experiment::AssetId{ change.guid.m_guid } ||
        publication->manifest.entries.front().asset.kind != cooked::CookedAssetKind::InputGraph)
    {
        return false;
    }
    const auto registry = SnapshotAssetMetaRegistry();
    if (!registry || registry->GetGuid(change.path) != change.guid)
    {
        // Removal/rename invalidates an older queued authoring publication.
        return false;
    }
    AssetDepot::AssetMountId previous;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            return false;
        }
        if (m_assetRootHandoffs != 0u || m_assetInvalidationDepth != 0u)
        {
            QueueAssetChange(change);
            return false;
        }
        if (const auto found = m_authoredInputGraphMounts.find(change.guid); found != m_authoredInputGraphMounts.end())
        {
            previous = found->second;
        }
    }
    cooked::AssetSetMountOptions options;
    options.expectedTargetPlatform = "win-x64";
    options.expectedTargetAbi = CreatorContentAbi::Token;
    const auto reference = publication->manifest.entries.front().asset;
    const auto catalog = GetCookedCatalog();
    cooked::ResolvedAssetEntry old;
    if (catalog && catalog->Find(reference, old) == cooked::AssetLookupStatus::Found)
    {
        if (old.blob == publication->manifest.blobs.front())
        {
            return true;
        }
        options.overrideIdentities.push_back(reference.key);
    }
    std::vector<cooked::AssetManifestIssue> issues;
    const auto mounted = MountAssetSet(publication->manifestBytes, publication->byteSource, options, issues, previous);
    if (!mounted.IsValid())
    {
        for (const auto& issue : issues)
        {
            if (issue.context == "mount" && issue.message.find("stale") != std::string::npos)
            {
                QueueAssetChange(change);
                return false;
            }
            Debug::PrintLog(spdlog::level::err, "InputGraph publication rejected [" + issue.context + "]: " + issue.message);
        }
        return false;
    }
    {
        std::lock_guard lock(m_assetPreparationMutex);
        m_authoredInputGraphMounts[change.guid] = mounted;
    }
    return true;
}
