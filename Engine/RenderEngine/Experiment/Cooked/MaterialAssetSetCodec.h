#pragma once

#include "CookedAssetManifest.h"
#include "../../MaterialGraphRuntime.h"

namespace experiment::cooked
{
    inline constexpr std::uint32_t kMaterialDocumentRepresentation = 1u;
    inline constexpr std::uint32_t kMaterialProgramRepresentation = 1u;
    inline constexpr std::size_t kMaterialDocumentMaxBytes = 16u * 1024u * 1024u;
    inline constexpr std::size_t kMaterialProgramAssetSetMaxBytes = 160u * 1024u * 1024u;

    struct MaterialAssetSetProduct final
    {
        TypedAssetReference asset{};
        std::vector<std::byte> artifactBytes{};
        std::vector<AssetDependency> dependencies{};
        std::uint32_t representation{};
        std::uint32_t schemaVersion{};
        std::string extension{};
    };

    // The current Material representation is a canonical Lattice instance
    // document, not an experiment::Material or a legacy reflected Material.
    // Global UUIDv4/v8 identities use a nil subassetId. Model producers can
    // encode an already-authored UUIDv8 document without reopening any source.
    // Fresh dependencies default to Internal; a recipe may explicitly choose
    // External later without changing the content-addressed document bytes.
    [[nodiscard]] bool EncodeMaterialAssetSetDocument(
        const material_graph::InstanceDocument& document, std::vector<std::byte>& outBytes,
        std::vector<AssetDependency>& outDependencies, std::string& failure);

    // Requires a decoded/verified product. Reads texture references directly
    // from program.resources, never from a legacy manifest dependency list.
    // Also checks the generated identity and both complete Scene backends.
    [[nodiscard]] bool CollectMaterialProgramAssetSetDependencies(
        const material_graph::VerifiedProduct& product, const AssetId& programAssetId,
        std::vector<AssetDependency>& outDependencies, std::string& failure);

    // Cross-check selected Material -> MaterialProgram products without a v2
    // manifest, texture I/O, renderer objects or a source fallback. The caller
    // separately resolves every typed Hard texture edge in its captured set.
    [[nodiscard]] bool ValidateMaterialAssetSetBinding(const material_graph::InstanceDocument& document,
        const material_graph::VerifiedProduct& product, std::string& failure);

    // Source-free, bounded and transactional. The caller first verifies the
    // exact blob hash, representation and schema in its captured catalog.
    // Every expected edge must be Hard with the exact typed target. Both
    // explicit scopes are accepted; duplicate, extra and missing edges fail.
    [[nodiscard]] bool ReadMaterialAssetSetDocument(std::span<const std::byte> bytes,
        const TypedAssetReference& expected, std::span<const AssetDependency> dependencies,
        material_graph::InstanceDocument& out, std::string& failure);

    [[nodiscard]] bool ReadMaterialProgramAssetSetArtifact(std::span<const std::byte> bytes,
        const TypedAssetReference& expected, std::span<const AssetDependency> dependencies,
        const material_graph::Budget& budget, material_graph::CookedProgram& out, std::string& failure);
}
