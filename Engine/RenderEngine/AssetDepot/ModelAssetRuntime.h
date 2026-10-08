#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Assets/ModelAnimationDescriptor.h"
#include "../Assets/ModelAnimationPayload.h"
#include "../Assets/ModelGeometryPayload.h"
#include "../Assets/ModelSceneAssetInputs.h"

#include <atomic>
#include <map>
#include <utility>

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
        // Nonzero only for compatible raw geometry/skeleton/clip decode entries.
        std::uint32_t decoderRecipe{};
        friend auto operator<=>(const ModelAssetKey&, const ModelAssetKey&) = default;
    };

    [[nodiscard]] inline ModelAssetKey MakeModelStorageKey(
        experiment::cooked::AssetBlobRecord blob, std::uint32_t decoderRecipe = 1u)
    {
        const auto kind = blob.kind;
        blob.artifactPath.clear();
        return { { {}, kind }, std::move(blob), 0u, true, decoderRecipe };
    }

    template<class T>
    struct ModelAssetWork;

    template<class T>
    struct ModelAssetDependencies {};

    template<>
    struct ModelAssetDependencies<assets::ModelGeometryPayload>
    {
        // Conditional collider flights have a fixed root/predecessor dependency
        // graph. Different root gates serialize instead of sharing a decision.
        own::shared_owner<ModelAssetWork<assets::ModelGeometryPayload>> predecessor{};
        AssetRequest<assets::ModelAnimationDescriptor> colliderDescriptor{};
        experiment::AssetId colliderMesh{};
        assets::ModelColliderPreparationPolicy colliderPolicy{ assets::ModelColliderPreparationPolicy::CookedDefault };
        std::vector<own::weak_owner<AssetRequestState<assets::ModelGeometryPayload>>> colliderConsumers{};
        bool colliderOnly{};
        bool ordinaryDemand{};
        bool colliderGateEvaluated{};
        bool colliderRequired{};
    };

    template<>
    struct ModelAssetDependencies<assets::ModelSkeletonPayload>
    {
        own::shared_owner<ModelAssetWork<assets::ModelSkeletonStorage>> storageWork{};
    };

    template<>
    struct ModelAssetDependencies<assets::ModelAnimationPayload>
    {
        own::shared_owner<ModelAssetWork<assets::ModelAnimationStorage>> storageWork{};
        // A real typed hard dependency; no owning void, provider or lease facade.
        own::shared_owner<ModelAssetWork<assets::ModelSkeletonPayload>> skeletonWork{};
    };

    template<>
    struct ModelAssetDependencies<assets::ModelMeshDescriptor>
    {
        own::shared_owner<ModelAssetWork<assets::ModelSkeletonPayload>> skeletonWork{};
        own::shared_owner<ModelAssetWork<assets::ModelGeometryPayload>> geometryWork{};
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
        // Eviction captures are released with the accepted work, after the
        // scheduler terminal observer has left the preparation lock.
        std::vector<own::shared_owner<const T>> retiredAssets{};
        // Written by this worker only; snapshots never read its mutable vectors.
        std::atomic<std::size_t> inputStagingBytes{};
        std::atomic<std::size_t> decodeStagingChargeBytes{};
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
        // Weak diagnostic index includes accepted work after logical invalidation
        // and completed dependency results until their final work pin is released.
        std::vector<own::weak_owner<ModelAssetWork<T>>> works{};
    };

    struct ModelAssetRetiredEntries final
    {
        ModelAssetEntries<assets::ModelAnimationDescriptor> descriptors{};
        ModelAssetEntries<assets::ModelSkeletonPayload> skeletons{};
        ModelAssetEntries<assets::ModelAnimationPayload> animations{};
        ModelAssetEntries<assets::ModelMeshDescriptor> meshes{};
        ModelAssetEntries<assets::ModelGeometryPayload> geometry{};
        ModelAssetEntries<assets::ModelSkeletonStorage> skeletonStorage{};
        ModelAssetEntries<assets::ModelAnimationStorage> animationStorage{};
        std::vector<own::shared_owner<AssetRequestState<assets::ModelAnimationDescriptor>>> descriptorConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelSkeletonPayload>>> skeletonConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelAnimationPayload>>> animationConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelMeshDescriptor>>> meshConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelGeometryPayload>>> geometryConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelSkeletonStorage>>> skeletonStorageConsumers;
        std::vector<own::shared_owner<AssetRequestState<assets::ModelAnimationStorage>>> animationStorageConsumers;
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
        std::size_t meshDescriptorRetainedBytes{};
        std::size_t meshDescriptorLiveBytes{};
        std::size_t geometryRetainedBytes{};
        std::size_t geometryLiveBytes{};
        std::size_t geometryBudgetBytes{};
        std::size_t skeletonDependencyBytes{};
        std::size_t geometryInFlight{};
        std::size_t skeletonStorageLiveBytes{};
        std::size_t animationStorageLiveBytes{};
        std::size_t skeletonStorageRetainedBytes{};
        std::size_t animationStorageRetainedBytes{};
        std::size_t skeletonStorageBudgetBytes{};
        std::size_t animationStorageBudgetBytes{};
        std::size_t skeletonStorageInFlight{};
        std::size_t animationStorageInFlight{};
        std::size_t inputStagingBytes{};
        std::size_t decodeStagingChargeBytes{};
        std::size_t completedWorkResultPinBytes{};
        // Input is the capacity of currently owned encoded read buffers. Decode
        // staging reports observed Skeleton/Clip intermediate vector capacities,
        // then the candidate's conservative ByteSize until publication. Codec
        // internal temporaries before a reader returns are not observable here.
        // Result pins count completed RAW work storage once per object, separate
        // from active staging and retained-cache charge. These categories can
        // overlap live storage; they must not be summed as physical memory.
        // Shared storage is counted once per compatible key, including payloads
        // pinned only by old logical owners after resolver invalidation. These
        // are decoded allocation charges, not measured process/allocator bytes.
        // Charges cover retained decoded vectors/strings and descriptor values,
        // not source backing, cache metadata, allocator overhead or GPU bytes.
        // Retained descriptor/clip charges conservatively include each hard
        // skeleton owner. meshDescriptorLiveBytes reports only small metadata;
        // skeletonDependencyBytes reports the live mesh closures separately.
        // Consumers remain live after cache-only logical eviction.
    };

    // Shared cache mechanics for the actual typed stores, in DataSystem's
    // existing value state. All accesses hold m_assetPreparationMutex.
    struct ModelAssetRuntimeState final
    {
        ModelAssetCache<assets::ModelAnimationDescriptor> descriptors{ {}, 0u, 4u * 1024u * 1024u };
        ModelAssetCache<assets::ModelSkeletonPayload> skeletons{ {}, 0u, 16u * 1024u * 1024u };
        ModelAssetCache<assets::ModelAnimationPayload> animations{ {}, 0u, 64u * 1024u * 1024u };
        ModelAssetCache<assets::ModelMeshDescriptor> meshes{ {}, 0u, 8u * 1024u * 1024u };
        ModelAssetCache<assets::ModelGeometryPayload> geometry{ {}, 0u, 128u * 1024u * 1024u };
        // Independent raw budgets bound cross-ID decode retention. Logical
        // budgets remain conservative and include their pinned shared closure.
        ModelAssetCache<assets::ModelSkeletonStorage> skeletonStorage{ {}, 0u, 16u * 1024u * 1024u };
        ModelAssetCache<assets::ModelAnimationStorage> animationStorage{ {}, 0u, 64u * 1024u * 1024u };
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
    };
}
