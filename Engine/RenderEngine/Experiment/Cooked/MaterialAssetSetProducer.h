#pragma once

#include "MaterialAssetSetCodec.h"

#include <optional>

namespace experiment::cooked
{
    inline constexpr std::size_t kMaterialAssetSetMetaMaxBytes = 1024u * 1024u;
    inline constexpr std::size_t kMaterialGraphSourceMaxBytes = 16u * 1024u * 1024u;

    struct MaterialAssetSetCookRequest final
    {
        AssetId materialAssetId{};
        std::span<const std::byte> sourceBytes{};
        std::span<const std::byte> metaBytes{};
    };

    struct MaterialProgramAssetSetCookRequest final
    {
        AssetId programAssetId{};
        std::span<const std::byte> sourceBytes{};
        std::span<const std::byte> metaBytes{};
        // Explicit, previously verified CookedProgram bytes. The producer
        // never finds an old package implicitly or invokes a shader compiler.
        std::span<const std::byte> verifiedProgramBytes{};
        material_graph::Budget budget{};
    };

    struct MaterialAssetSetCookResult final
    {
        std::optional<MaterialAssetSetProduct> product{};
        // Domain/version + length-framed role, byte length and SHA-256 for
        // each supplied blob. The caller includes its normalized input paths
        // in the enclosing inventory and owns the stable snapshot spans.
        Sha256Digest sourceInputsSha256{};
        std::string failure{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return product.has_value() && failure.empty();
        }
    };

    // Fresh typed v3 recipes. Source bytes are authoring text; output bytes
    // use the shared source-free codec. Standalone .asset sources require v4
    // in the caller; this byte boundary also supports model UUIDv8 sources.
    [[nodiscard]] MaterialAssetSetCookResult BuildMaterialAssetSetProduct(
        const MaterialAssetSetCookRequest& request);

    [[nodiscard]] MaterialAssetSetCookResult BuildMaterialProgramAssetSetProduct(
        const MaterialProgramAssetSetCookRequest& request);
}
