#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif

#include "ModelAssetGeneration.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets
{
    // One identity-free skeleton decode. Logical generations borrow the same
    // immutable arrays, but never inherit another generation's origin or ID.
    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ModelSkeletonStorage final
    {
        ModelSkeletonAsset skeleton{}; // skeletonId stays nil in shared storage.
        experiment::cooked::Sha256Digest boneLayoutSha256{};
        experiment::cooked::Sha256Digest skinBindingSha256{};
        // Canonical verified backing stays a matched source/locator pair.
        own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};
        std::string artifactPath{};
        std::size_t decodedBytes{};

        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };

    // Logical skeleton generation. Identity is origin.entry.asset; the legacy
    // sampling adapter below deliberately carries no logical skeletonId.
    struct [[reflgen::runtime, reflgen::public_fields, reflgen::public_methods]] ModelSkeletonPayload final
    {
        ModelSkeletonPayload(own::shared_owner<const ModelSkeletonStorage> decoded,
            experiment::cooked::ResolvedAssetEntry resolved, std::size_t charge)
            : storage(std::move(decoded)),
              skeleton(storage ? storage->skeleton : throw std::invalid_argument("Missing skeleton storage.")),
              boneLayoutSha256(storage->boneLayoutSha256), skinBindingSha256(storage->skinBindingSha256),
              origin(std::move(resolved)), decodedBytes(charge) {}
        ModelSkeletonPayload(const ModelSkeletonPayload&) = delete;
        ModelSkeletonPayload& operator=(const ModelSkeletonPayload&) = delete;
        ModelSkeletonPayload(ModelSkeletonPayload&&) = delete;
        ModelSkeletonPayload& operator=(ModelSkeletonPayload&&) = delete;

        const own::shared_owner<const ModelSkeletonStorage> storage;
        const ModelSkeletonAsset& skeleton;
        const experiment::cooked::Sha256Digest& boneLayoutSha256;
        const experiment::cooked::Sha256Digest& skinBindingSha256;
        experiment::cooked::ResolvedAssetEntry origin{};
        std::size_t decodedBytes{};

        // Conservative logical-cache charge includes the pinned shared arrays.
        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };
}
