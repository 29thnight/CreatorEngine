#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Texture.h"
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

    enum class TextureMipPolicy : std::uint8_t
    {
        PreserveAuthored,
        GenerateFull,
    };

    // These remain part of the runtime identity even when two options happen to
    // decode to equal bytes. Dependencies use their own default representation.
    struct TextureAssetVariant final
    {
        TextureAssetColorSpace colorSpace{ TextureAssetColorSpace::Source };
        bool compress{};
        std::uint32_t role{};
        TextureMipPolicy mipPolicy{ TextureMipPolicy::PreserveAuthored };
        bool forceRgba8{};
        friend auto operator<=>(const TextureAssetVariant&, const TextureAssetVariant&) noexcept = default;
    };

    // Byte compatibility excludes logical ID, resolver, path and role labels.
    // Version 1 fixes DirectXTex decode/lowering, BC1 dither/uniform threshold
    // 0.5, and non-WIC linear-light mip filtering with unchanged base mip.
    struct TextureImageRecipe final
    {
        std::uint32_t version{ 1u };
        bool compress{};
        bool forceRgba8{};
        TextureAssetColorSpace compressionColorSpace{ TextureAssetColorSpace::Source };
        TextureMipPolicy mipPolicy{ TextureMipPolicy::PreserveAuthored };
        TextureAssetColorSpace mipColorSpace{ TextureAssetColorSpace::Source };
        friend auto operator<=>(const TextureImageRecipe&, const TextureImageRecipe&) noexcept = default;
    };

    struct TextureImageKey final
    {
        experiment::cooked::Sha256Digest contentSha256{};
        std::uint64_t byteSize{};
        experiment::cooked::CookedAssetKind kind{ experiment::cooked::CookedAssetKind::Texture };
        std::uint32_t representation{};
        std::uint32_t schemaVersion{};
        std::string targetPlatform{};
        std::string targetAbi{};
        TextureImageRecipe recipe{};
        friend auto operator<=>(const TextureImageKey&, const TextureImageKey&) = default;
    };

    [[nodiscard]] TextureImageKey MakeTextureImageKey(
        const experiment::cooked::AssetBlobRecord& blob, const TextureAssetVariant& variant);

    // The path always travels with the source that captured and verified it.
    struct TextureImageSource final
    {
        own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};
        std::string artifactPath{};
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
    // owner pins the selected backing across logical unmount, independently of bulk.
    struct TextureAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
        TextureAssetVariant variant{};
        TextureImageKey imageKey{};
        own::shared_owner<const TextureImageSource> imageSource{};
        std::vector<own::shared_owner<const Texture>> hardDependencies{};
        std::vector<experiment::cooked::TypedAssetReference> loadableDependencies{};
        // Conservative child-closure descriptor charge; decoded image bytes have
        // their independent budget. Shared metadata may be counted twice.
        std::size_t hardDependencyChargeBytes{};
    };

    struct TextureImageWork final
    {
        TextureImageKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const TextureImageSource> source{};
        std::vector<own::weak_owner<AssetRequestState<Texture::CodecImage>>> consumers{};
        std::uint64_t requestId{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const Texture::CodecImage> image{};
    };

    struct TextureImageCacheEntry final
    {
        own::weak_owner<const Texture::CodecImage> live{};
        own::shared_owner<const Texture::CodecImage> retained{};
        own::shared_owner<const TextureImageSource> source{};
        own::shared_owner<TextureImageWork> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };
    using TextureImageEntries = std::map<TextureImageKey, TextureImageCacheEntry>;

    struct TextureAssetWork final
    {
        TextureAssetKey key{};
        own::shared_owner<TextureImageWork> imageWork{};
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
        TextureImageEntries images;
        std::vector<own::shared_owner<const Texture>> descriptorPins;
        std::vector<own::shared_owner<const Texture::CodecImage>> imagePins;
        std::vector<own::shared_owner<AssetRequestState<Texture::CodecImage>>> imageConsumers;
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
        // Descriptor charges exclude independently evictable image bytes and GPU
        // allocations. liveEntries counts only currently indexed generations.
        // Charges are not actual allocator/RHI bytes freed. Consumers and
        // accepted jobs can continue owning logically evicted generations.
    };

    struct TextureImageCacheSnapshot final
    {
        std::size_t entries{};
        std::size_t retainedEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::size_t liveBytes{};
        std::size_t nonRehydratableBytes{};
        std::size_t livePayloads{};
        std::size_t inFlightResultBytes{};
        std::uint64_t logicalEvictions{};
        // Live pixel accounting follows payload lifetime, including old owners.
        // Retained charge conservatively adds SDK/table/key/source metadata and
        // is neither measured allocator usage nor bytes actually freed.
    };

    // Value state inside DataSystem, never another asset manager or registry.
    // Every access, including work state, uses m_assetPreparationMutex.
    struct TextureAssetRuntimeState final
    {
        TextureAssetEntries entries{};
        std::size_t budgetBytes{ 4u * 1024u * 1024u };
        TextureImageEntries images{};
        std::vector<own::weak_owner<AssetRequestState<Texture::CodecImage>>> imageRequests{};
        std::size_t imageBudgetBytes{ 64u * 1024u * 1024u };
        std::size_t retainedImageBytes{};
        std::uint64_t imageEvictions{};
        std::size_t retainedChargeBytes{};
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
        std::uint64_t logicalEvictions{};
    };
}
