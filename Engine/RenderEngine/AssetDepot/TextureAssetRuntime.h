#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../../Utility_Framework/JobScheduler.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Texture;

namespace AssetDepot
{
    enum class TextureAssetColorSpace : std::uint8_t
    {
        Source,
        Linear,
        Srgb,
    };

    // These remain part of the runtime identity even when two options happen to
    // decode to equal bytes. Dependencies use their own default representation.
    struct TextureAssetVariant final
    {
        TextureAssetColorSpace colorSpace{ TextureAssetColorSpace::Source };
        bool compress{};
        std::uint32_t role{};
        friend auto operator<=>(const TextureAssetVariant&, const TextureAssetVariant&) noexcept = default;
    };

    struct TextureAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        TextureAssetVariant variant{};
        friend auto operator<=>(const TextureAssetKey&, const TextureAssetKey&) = default;
    };

    // Exact provenance travels with the immutable Texture generation. The source
    // owner pins the selected backing across logical unmount. This first slice
    // still pins CodecImage in Texture; descriptor/bulk eviction is not complete.
    struct TextureAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
        TextureAssetVariant variant{};
        std::vector<own::shared_owner<const Texture>> hardDependencies{};
        std::vector<experiment::cooked::TypedAssetReference> loadableDependencies{};
        // Conservative child-closure decoded-byte charge; the cache adds own bytes.
        // Shared children may be counted twice, but a retained parent never
        // hides bytes still pinned after a child's cache entry is evicted.
        std::size_t hardDependencyChargeBytes{};
    };

    struct TextureAssetWork final
    {
        TextureAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog{};
        std::vector<own::shared_owner<TextureAssetWork>> dependencies{};
        std::vector<own::weak_owner<AssetRequestState<Texture>>> consumers{};
        std::uint64_t epoch{};
        std::uint64_t requestId{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const Texture> asset{};
    };

    struct TextureAssetCacheEntry final
    {
        own::weak_owner<const Texture> live{};
        own::shared_owner<const Texture> retained{};
        own::shared_owner<TextureAssetWork> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };

    using TextureAssetEntries = std::map<TextureAssetKey, TextureAssetCacheEntry>;

    // Caller constructs this before a root transaction. Stage fills consumerPins
    // before mutation; detached nodes and cancellation pins die after outer unlock.
    struct TextureAssetRetiredEntries final
    {
        TextureAssetEntries entries;
        std::vector<own::shared_owner<AssetRequestState<Texture>>> consumerPins;
    };

    struct TextureAssetCacheSnapshot final
    {
        std::size_t entries{};
        std::size_t retainedEntries{};
        std::size_t liveEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
        // Charges cover retained decoded pixels, not source backing, metadata or GPU
        // allocations. liveEntries counts only currently indexed generations.
        // Charges are not actual allocator/RHI bytes freed. Consumers and
        // accepted jobs can continue owning logically evicted generations.
    };

    // Value state inside DataSystem, never another asset manager or registry.
    // Every access, including work state, uses m_assetPreparationMutex.
    struct TextureAssetRuntimeState final
    {
        TextureAssetEntries entries{};
        std::size_t budgetBytes{ 64u * 1024u * 1024u };
        std::size_t retainedChargeBytes{};
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
        std::uint64_t logicalEvictions{};
    };
}
