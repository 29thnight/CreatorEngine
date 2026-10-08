#pragma once

#include "ModelSkeletonPayload.h"

namespace assets
{
    // Published once after the ordered bone-layout contract is validated.
    // The track table borrows only from this object's immutable clip storage.
    struct ModelAnimationPayload final
    {
        ModelAnimationPayload() = default;
        ModelAnimationPayload(const ModelAnimationPayload&) = delete;
        ModelAnimationPayload& operator=(const ModelAnimationPayload&) = delete;
        ModelAnimationPayload(ModelAnimationPayload&&) = delete;
        ModelAnimationPayload& operator=(ModelAnimationPayload&&) = delete;

        ModelAnimationAsset clip{};
        own::shared_owner<const ModelSkeletonPayload> skeleton{};
        std::vector<const ModelAnimationTrack*> tracks{};
        experiment::cooked::ResolvedAssetEntry origin{};
        std::size_t decodedBytes{};

        // Includes the hard dependency conservatively for cache budgeting.
        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };
}
