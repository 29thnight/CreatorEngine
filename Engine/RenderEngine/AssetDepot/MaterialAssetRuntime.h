#pragma once

#include "AssetLink.h"
#include "AssetRequest.h"
#include "../Experiment/Cooked/CookedAssetCatalog.h"
#include "../MaterialGraphRuntime.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace AssetDepot
{
    struct ShaderMetaAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        friend auto operator<=>(const ShaderMetaAssetKey&, const ShaderMetaAssetKey&) = default;
    };

    // The immutable descriptor pins only its exact captured artifact source.
    // Its typed Loadable declarations stay in resolved.entry.dependencies;
    // metadata readiness does not request those assets or compile source code.
    struct ShaderMetaAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
        // Present only on accepted code-program metadata snapshots. The
        // descriptor identity stays separate; this pins compiled blob backing.
        std::optional<experiment::cooked::ResolvedAssetEntry> codeProgramSource{};
        std::vector<own::shared_owner<const Texture>> codeProgramTextures{};
    };

    struct ShaderMetaAssetWork final
    {
        ShaderMetaAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog{};
        std::vector<own::weak_owner<AssetRequestState<ShaderMeta>>> consumers{};
        std::uint64_t epoch{};
        std::uint64_t requestId{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const ShaderMeta> asset{};
        // Scheduler captures outlive their completion lock. Evicted source and
        // metadata owners are released with those captures, after outer unlock.
        std::vector<own::shared_owner<const ShaderMeta>> retiredAssets{};
    };

    struct ShaderMetaAssetCacheEntry final
    {
        own::weak_owner<const ShaderMeta> live{};
        own::shared_owner<const ShaderMeta> retained{};
        own::shared_owner<ShaderMetaAssetWork> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };

    using ShaderMetaAssetEntries = std::map<ShaderMetaAssetKey, ShaderMetaAssetCacheEntry>;

    struct MaterialPipelineAssetKey final
    {
        experiment::cooked::TypedAssetReference asset{};
        experiment::cooked::AssetBlobRecord blob{};
        std::uint64_t resolverRevision{};
        friend auto operator<=>(const MaterialPipelineAssetKey&, const MaterialPipelineAssetKey&) = default;
    };

    struct MaterialAssetTexturePin final
    {
        experiment::AssetId assetId{};
        LX::LXColorSpace colorSpace{};
        own::shared_owner<const Texture> owner{};
    };

    struct MaterialProgramAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
        std::vector<MaterialAssetTexturePin> defaultTextures{};
        // Calculated from the actual immutable Texture pins before publication.
        // Shared representations can be overcharged, never hidden as pointers.
        std::size_t defaultTextureChargeBytes{};
        // The code program owns the exact independently decoded descriptor.
        own::shared_owner<const ShaderMeta> shaderMetadata{};
    };

    struct MaterialDocumentAssetOrigin final
    {
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const LX::Runtime::ShaderGeneration> codeProgram{};
        std::vector<MaterialAssetTexturePin> textures{};
    };

    struct MaterialAssetTextureRequest final
    {
        experiment::AssetId assetId{};
        AssetRequest<Texture> request{};
    };

    template<class T>
    struct MaterialPipelineAssetWork final
    {
        MaterialPipelineAssetKey key{};
        experiment::cooked::ResolvedAssetEntry resolved{};
        own::shared_owner<const experiment::cooked::CookedAssetCatalog> catalog{};
        std::vector<MaterialAssetTextureRequest> textures{};
        // Empty for a Program leaf; a Material owns its exact Program request.
        AssetRequest<material_graph::Generation> program{};
        AssetRequest<LX::Runtime::ShaderGeneration> codeProgram{};
        AssetRequest<experiment::Material> authoredMaterial{};
        // Captured typed metadata leaf, read only by the accepted CPU worker.
        std::optional<experiment::cooked::ResolvedAssetEntry> shaderMetadata{};
        std::vector<own::weak_owner<AssetRequestState<T>>> consumers{};
        std::uint64_t epoch{};
        std::uint64_t requestId{};
        std::uint64_t programGeneration{};
        job_handle completion{};
        AssetRequestStatus status{ AssetRequestStatus::Pending };
        AssetRequestError error{ AssetRequestError::None };
        std::string message{};
        own::shared_owner<const T> asset{};
        std::vector<own::shared_owner<const T>> retiredAssets{};
    };

    template<class T>
    struct MaterialPipelineAssetCacheEntry final
    {
        own::weak_owner<const T> live{};
        own::shared_owner<const T> retained{};
        own::shared_owner<MaterialPipelineAssetWork<T>> inFlight{};
        std::size_t retainedCharge{};
        std::uint64_t lastUse{};
    };

    template<class T>
    using MaterialPipelineAssetEntries = std::map<MaterialPipelineAssetKey, MaterialPipelineAssetCacheEntry<T>>;

    template<class T>
    struct MaterialPipelineAssetCache final
    {
        MaterialPipelineAssetEntries<T> entries{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
        std::uint64_t clock{};
    };

    // Staging may allocate, before a root transaction mutates anything. The
    // detached map and consumer pins must be destroyed after the outer locks.
    struct MaterialAssetRetiredEntries final
    {
        ShaderMetaAssetEntries shaderMetadata{};
        std::vector<own::shared_owner<AssetRequestState<ShaderMeta>>> shaderMetaConsumers{};
        MaterialPipelineAssetEntries<material_graph::Generation> programs{};
        MaterialPipelineAssetEntries<Material> materials{};
        MaterialPipelineAssetEntries<LX::Runtime::ShaderGeneration> codePrograms{};
        MaterialPipelineAssetEntries<experiment::Material> authoredMaterials{};
        std::vector<own::shared_owner<AssetRequestState<material_graph::Generation>>> programConsumers{};
        std::vector<own::shared_owner<AssetRequestState<Material>>> materialConsumers{};
        std::vector<own::shared_owner<AssetRequestState<LX::Runtime::ShaderGeneration>>> codeProgramConsumers{};
        std::vector<own::shared_owner<AssetRequestState<experiment::Material>>> authoredMaterialConsumers{};
    };

    struct ShaderMetaAssetCacheSnapshot final
    {
        std::size_t inputStagingBytes{};
        std::size_t entries{};
        std::size_t retainedEntries{};
        std::size_t liveEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
        // Charges cover descriptor/origin values and owned string/vector
        // capacities, not catalog/source backing, cache nodes or GPU memory.
        // Logical eviction does not report bytes reclaimed from consumer pins.
    };

    struct MaterialPipelineAssetCacheStatistics final
    {
        std::size_t entries{};
        std::size_t retainedEntries{};
        std::size_t liveEntries{};
        std::size_t inFlight{};
        std::size_t retainedChargeBytes{};
        std::size_t budgetBytes{};
        std::uint64_t logicalEvictions{};
    };

    struct MaterialAssetCacheSnapshot final
    {
        std::size_t inputStagingBytes{};
        MaterialPipelineAssetCacheStatistics programs{};
        MaterialPipelineAssetCacheStatistics materials{};
        MaterialPipelineAssetCacheStatistics codePrograms{};
        MaterialPipelineAssetCacheStatistics authoredMaterials{};
        // Conservative reachable CPU capacity charges include hard owners;
        // shared dependencies can be charged more than once. No GPU/bulk bytes
        // are claimed reclaimed when a retained cache owner is released.
    };

    // Concrete typed value state in DataSystem, not another manager. This first
    // material pipeline supports metadata, graph and verified code Programs,
    // Lattice facades and authored experiment::Material values.
    // All accesses use DataSystem's existing m_assetPreparationMutex.
    struct MaterialAssetRuntimeState final
    {
        // Actual observed input vector capacity during accepted reads/decodes;
        // independent of retained closure charges and shared payload residency.
        std::atomic<std::size_t> shaderMetaInputStagingBytes{};
        std::atomic<std::size_t> materialInputStagingBytes{};
        ShaderMetaAssetEntries shaderMetadata{};
        std::size_t shaderMetaBudgetBytes{ 4u * 1024u * 1024u };
        std::size_t shaderMetaRetainedChargeBytes{};
        std::uint64_t clock{};
        std::uint64_t nextRequestId{ 1u };
        std::uint64_t shaderMetaLogicalEvictions{};
        MaterialPipelineAssetCache<material_graph::Generation> programs{ {}, 0u, 128u * 1024u * 1024u };
        MaterialPipelineAssetCache<Material> materials{ {}, 0u, 32u * 1024u * 1024u };
        MaterialPipelineAssetCache<LX::Runtime::ShaderGeneration> codePrograms{ {}, 0u, 128u * 1024u * 1024u };
        MaterialPipelineAssetCache<experiment::Material> authoredMaterials{ {}, 0u, 32u * 1024u * 1024u };
    };
}
