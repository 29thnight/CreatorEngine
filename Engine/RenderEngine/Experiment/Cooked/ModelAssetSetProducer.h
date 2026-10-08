#pragma once

#include "CookedModelSubAssetCodec.h"
#include <filesystem>

namespace experiment::cooked
{
    struct ModelAssetSetCookRequest final
    {
        std::filesystem::path sourcePath{};
        std::filesystem::path assetRoot{};
        std::filesystem::path identityHeaderPath{};
        // Already-authored global UUIDv8 model/subasset identities. No ordinals.
        std::vector<TypedAssetReference> selected{};
    };

    struct ModelAssetSetProduct final
    {
        TypedAssetReference asset{};
        std::vector<std::byte> artifactBytes{};
        std::vector<AssetDependency> dependencies{};
        std::uint32_t representation{};
        std::uint32_t schemaVersion{};
        std::string extension{};
    };

    struct ModelAssetSetCookResult final
    {
        std::vector<ModelAssetSetProduct> products{};
        // Length-framed ordered path+hash inventory: root source, canonical
        // sidecar, epoch header and every captured external importer input.
        Sha256Digest sourceInputsSha256{};
        std::string failure{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return failure.empty() && !products.empty();
        }
    };

    // Fresh source import once per request. No canonical files are changed,
    // no generation/model.cemc or v2 package is read, and only selections emit.
    [[nodiscard]] ModelAssetSetCookResult BuildModelAssetSetProducts(
        const ModelAssetSetCookRequest& request);
}
