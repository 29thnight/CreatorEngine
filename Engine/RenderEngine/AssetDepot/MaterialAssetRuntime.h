#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace AssetDepot
{
    struct ShaderMetaAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        friend auto operator<=>(const ShaderMetaAssetKey&, const ShaderMetaAssetKey&) = default;
    };

    // The immutable descriptor pins only its exact captured artifact source.
    // Its typed Loadable declarations stay in resolved.entry.dependencies;
    // metadata readiness does not request those assets or compile source code.
    struct ShaderMetaAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
    };

    struct ShaderMetaAssetWork final
    {
        ShaderMetaAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog{};
        std::vector<own::weak_owner<AssetRequestState<ShaderMeta>>> consumers{};
        std::uint64_t epoch{};
        std::uint64_t requestId{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const ShaderMeta> asset{};
        // Scheduler captures outlive their completion lock. Evicted source and
        // metadata owners are released with those captures, after outer unlock.
        std::vector<own::shared_owner<const ShaderMeta>> retiredAssets{};
    };

    struct ShaderMetaAssetCacheEntry final
    {
        own::weak_owner<const ShaderMeta> live{};
        own::shared_owner<const ShaderMeta> retained{};
        own::shared_owner<ShaderMetaAssetWork> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };

    using ShaderMetaAssetEntries = std::map<ShaderMetaAssetKey, ShaderMetaAssetCacheEntry>;

    // Staging may allocate, before a root transaction mutates anything. The
    // detached map and consumer pins must be destroyed after the outer locks.
    struct MaterialAssetRetiredEntries final
    {
        ShaderMetaAssetEntries shaderMetadata{};
        std::vector<own::shared_owner<AssetRequestState<ShaderMeta>>> shaderMetaConsumers{};
    };

    struct ShaderMetaAssetCacheSnapshot final
    {
        std::size_t entries{};
        std::size_t retainedEntries{};
        std::size_t liveEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
        // Charges cover descriptor/origin values and owned string/vector
        // capacities, not catalog/source backing, cache nodes or GPU memory.
        // Logical eviction does not report bytes reclaimed from consumer pins.
    };

    // Concrete typed value state in DataSystem, not another manager. This first
    // material-pipeline leaf supports only authored ShaderMeta documents.
    // All accesses use DataSystem's existing m_assetPreparationMutex.
    struct MaterialAssetRuntimeState final
    {
        ShaderMetaAssetEntries shaderMetadata{};
        std::size_t shaderMetaBudgetBytes{ 4u * 1024u * 1024u };
        std::size_t shaderMetaRetainedChargeBytes{};
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
        std::uint64_t shaderMetaLogicalEvictions{};
    };
}
