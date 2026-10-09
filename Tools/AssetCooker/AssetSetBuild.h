#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace AssetCooking
{
    // Source-only CEMF v3 build. Legacy package manifests are never inputs.
    struct AssetSetBuildRequest final
    {
        std::filesystem::path assetRoot{};
        std::filesystem::path definitionPath{};
        std::filesystem::path outputRoot{};
        std::filesystem::path artifactCache{};
        // Digest of the verified toolchain, including native decoder dependencies.
        std::string toolFingerprint{};
    };

    struct AssetSetBuildResult final
    {
        bool succeeded{};
        std::size_t assets{};
        std::size_t blobs{};
        std::size_t reusedBlobs{};
        // Source transactions, not CAS deduplication. A model selection group
        // is one transaction; typed artifact readback still runs on a hit.
        std::size_t reusedImports{};
        std::size_t recookedImports{};
        std::string failure{};
    };

    // Emits a new immutable directory only after source cook, cache verification
    // and manifest/payload readback. A failure never replaces a prior output.
    [[nodiscard]] AssetSetBuildResult BuildAssetSet(const AssetSetBuildRequest& request);
}
