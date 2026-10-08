#pragma once

#include "ModelSkeletonPayload.h"

namespace assets
{
    // Clip bytes contain a skeleton binding. Only the same compatible blob can
    // share this storage; keyframes alone are not a sufficient reuse identity.
    struct ModelAnimationStorage final
    {
        ModelAnimationStorage() = default;
        ModelAnimationStorage(const ModelAnimationStorage&) = delete;
        ModelAnimationStorage& operator=(const ModelAnimationStorage&) = delete;
        ModelAnimationStorage(ModelAnimationStorage&&) = delete;
        ModelAnimationStorage& operator=(ModelAnimationStorage&&) = delete;

        ModelAnimationAsset clip{}; // animationId stays nil in shared storage.
        experiment::AssetId skeletonAssetId{};
        experiment::cooked::Sha256Digest requiredBoneLayoutSha256{};
        std::uint32_t requiredBoneCount{};
        // Borrows from this object's immutable clip, never a logical wrapper.
        std::vector<const ModelAnimationTrack*> tracks{};
        own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};
        std::string artifactPath{};
        std::size_t decodedBytes{};

        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };

    // Published only after this logical generation's exact hard skeleton has
    // passed identity and ordered-layout validation against the decoded binding.
    struct ModelAnimationPayload final
    {
        ModelAnimationPayload(own::shared_owner<const ModelAnimationStorage> decoded,
            own::shared_owner<const ModelSkeletonPayload> hardSkeleton,
            experiment::cooked::ResolvedAssetEntry resolved, std::size_t charge)
            : storage(std::move(decoded)),
              clip(storage ? storage->clip : throw std::invalid_argument("Missing animation storage.")),
              skeleton(std::move(hardSkeleton)), tracks(storage->tracks),
              origin(std::move(resolved)), decodedBytes(charge) {}
        ModelAnimationPayload(const ModelAnimationPayload&) = delete;
        ModelAnimationPayload& operator=(const ModelAnimationPayload&) = delete;
        ModelAnimationPayload(ModelAnimationPayload&&) = delete;
        ModelAnimationPayload& operator=(ModelAnimationPayload&&) = delete;

        const own::shared_owner<const ModelAnimationStorage> storage;
        const ModelAnimationAsset& clip;
        const own::shared_owner<const ModelSkeletonPayload> skeleton;
        const std::vector<const ModelAnimationTrack*>& tracks;
        experiment::cooked::ResolvedAssetEntry origin{};
        std::size_t decodedBytes{};

        // Identity is origin.entry.asset, not the identity-free sampling adapter.
        // Conservative logical-cache charge includes storage and hard skeleton.
        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };
}
