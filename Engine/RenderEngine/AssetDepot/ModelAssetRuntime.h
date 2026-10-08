#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Assets/ModelAnimationDescriptor.h"
#include "../Assets/ModelAnimationPayload.h"

#include <map>

namespace AssetDepot
{
    struct ModelAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        // Exact-owner admission is separate from current-link lookup. Old
        // generations enter through the descriptor's captured locator/source,
        // never by consulting the latest resolver for an unresolved child.
        bool exactGeneration{};
        friend auto operator<=>(const ModelAssetKey&, const ModelAssetKey&) = default;
    };

    template<class T>
    struct ModelAssetWork;

    template<class T>
    struct ModelAssetDependencies {};

    template<>
    struct ModelAssetDependencies<assets::ModelAnimationPayload>
    {
        // A real typed hard dependency; no owning void, provider or lease facade.
        own::shared_owner<ModelAssetWork<assets::ModelSkeletonPayload>> skeletonWork{};
    };

    template<class T>
    struct ModelAssetWork final
    {
        ModelAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog{};
        ModelAssetDependencies<T> dependencies{};
        std::vector<own::weak_owner<AssetRequestState<T>>> consumers{};
        std::uint64_t epoch{};
        std::uint64_t requestId{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const T> asset{};
    };

    template<class T>
    struct ModelAssetCacheEntry final
    {
        own::weak_owner<const T> live{};
        own::shared_owner<const T> retained{};
        own::shared_owner<ModelAssetWork<T>> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };

    template<class T>
    using ModelAssetEntries = std::map<ModelAssetKey, ModelAssetCacheEntry<T>>;

    template<class T>
    struct ModelAssetCache final
    {
        ModelAssetEntries<T> entries{};
        std::size_t retainedBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
    };

    struct ModelAssetRetiredEntries final
    {
        ModelAssetEntries<assets::ModelAnimationDescriptor> descriptors{};
        ModelAssetEntries<assets::ModelSkeletonPayload> skeletons{};
        ModelAssetEntries<assets::ModelAnimationPayload> animations{};
        std::vector<own::shared_owner<AssetRequestState<assets::ModelAnimationDescriptor>>> descriptorConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelSkeletonPayload>>> skeletonConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelAnimationPayload>>> animationConsumers;
    };

    struct ModelAssetCacheSnapshot final
    {
        std::size_t entries{};
        std::size_t liveEntries{};
        std::size_t retainedEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
        // Charges cover retained decoded vectors/strings and descriptor values,
        // not source backing, cache metadata, allocator overhead or GPU bytes.
        // Hard dependencies are conservatively counted again with each clip.
        // Consumers remain live after cache-only logical eviction.
    };

    // Shared cache mechanics for three actual typed stores, in DataSystem's
    // existing value state. All accesses hold m_assetPreparationMutex.
    struct ModelAssetRuntimeState final
    {
        ModelAssetCache<assets::ModelAnimationDescriptor> descriptors{ {}, 0u, 4u * 1024u * 1024u };
        ModelAssetCache<assets::ModelSkeletonPayload> skeletons{ {}, 0u, 16u * 1024u * 1024u };
        ModelAssetCache<assets::ModelAnimationPayload> animations{ {}, 0u, 64u * 1024u * 1024u };
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
    };
}
