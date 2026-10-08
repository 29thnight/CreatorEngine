#pragma once

#include "ModelAssetGeneration.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"

namespace assets
{
    // One skeleton artifact. No clip, geometry or image arrays are retained here.
    struct ModelSkeletonPayload final
    {
        ModelSkeletonAsset skeleton{};
        experiment::cooked::Sha256Digest boneLayoutSha256{};
        experiment::cooked::ResolvedAssetEntry origin{};
        std::size_t decodedBytes{};

        [[nodiscard]] std::size_t ByteSize() const noexcept { return decodedBytes; }
    };
}
