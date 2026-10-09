#pragma once

#include "MaterialAssetSetCodec.h"
#include "CookedCodeMaterial.h"

#include <optional>

namespace experiment::cooked
{
    inline constexpr std::size_t kMaterialAssetSetMetaMaxBytes = 1024u * 1024u;
    inline constexpr std::size_t kMaterialGraphSourceMaxBytes = 16u * 1024u * 1024u;

    struct CodeProgramCapturedInput final
    {
        std::string path;
        std::span<const std::byte> bytes;
    };

    struct MaterialAssetSetCookRequest final
    {
        AssetId materialAssetId{};
        std::span<const std::byte> sourceBytes{};
        std::span<const std::byte> metaBytes{};
        // Explicit Code binding for the existing flat experiment schema1.
        // A verified bundle supplies defaults and validates the binding.
        AssetId programAssetId{};
        std::span<const AssetId> defaultTextureAssetIds{};
        std::span<const std::byte> verifiedProgramBytes{};
        std::span<const CodeProgramCapturedInput> codeInputs{};
    };

    struct MaterialProgramAssetSetCookRequest final
    {
        AssetId programAssetId{};
        std::span<const std::byte> sourceBytes{};
        std::span<const std::byte> metaBytes{};
        // Explicit, previously verified Lattice CookedProgram or CodeProgram
        // bytes. No implicit old-package lookup or shader compilation.
        std::span<const std::byte> verifiedProgramBytes{};
        material_graph::Budget budget{};
        // Full immutable source inventory for a .codeprogram recipe, sorted by
        // normalized Assets-relative path. No file I/O/compilation below here.
        std::span<const CodeProgramCapturedInput> codeInputs{};
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
    // use the shared source-free codec. Authored schema1 binds an explicit Code
    // bundle; .codeprogram recipes validate its full captured input inventory.
    // Standalone .asset sources require v4
    // in the caller; this byte boundary also supports model UUIDv8 sources.
    [[nodiscard]] MaterialAssetSetCookResult BuildMaterialAssetSetProduct(
        const MaterialAssetSetCookRequest& request);

    [[nodiscard]] MaterialAssetSetCookResult BuildMaterialProgramAssetSetProduct(
        const MaterialProgramAssetSetCookRequest& request);
}
