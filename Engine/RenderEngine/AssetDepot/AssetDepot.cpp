#include "../DataSystem.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

#include <limits>
#include <utility>

AssetDepot::AssetMountId DataSystem::MountAssetSet(
    std::span<const std::byte> manifestBytes,
    own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource,
    const experiment::cooked::AssetSetMountOptions& options,
    std::vector<experiment::cooked::AssetManifestIssue>& outIssues)
{
    namespace cooked = experiment::cooked;
    outIssues.clear();
    cooked::AssetSetManifest manifest;
    if (!cooked::ReadAssetSetManifest(manifestBytes, manifest, outIssues))
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
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
        {
            outIssues.push_back({ "mount", "asset admission has stopped" });
            return {};
        }
        if (m_assetDepotRevision == (std::numeric_limits<std::uint64_t>::max)()
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
    auto published = own::make_shared<const cooked::CookedAssetCatalog>(std::move(candidate));
    own::shared_owner<const cooked::CookedAssetCatalog> retired;
    AssetDepot::TextureAssetRetiredEntries retiredTextures;
    AssetDepot::ModelAssetRetiredEntries retiredModels;
    AssetDepot::MaterialAssetRetiredEntries retiredMaterials;
    LegacyCacheRetirement retiredLegacy;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != epoch
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
        m_assetDepotRevision = revision + 1u;
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
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || !m_cookedCatalog)
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
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != epoch
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
