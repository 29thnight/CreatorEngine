#include "../../Engine/RenderEngine/MaterialGraphInstancePins.h"
#include "../../Engine/RenderEngine/Texture.h"
#include "material_owner_checks.h"
#include "material_runtime_tests.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.h"
#include "../../Engine/Utility_Framework/AuthoringWriteNode.h"

#include <atomic>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

void VerifyMaterialRuntime(const material_graph::VerifiedProduct& numericProduct,
                           const experiment::cooked::CookedAssetCatalog& catalog,
                           const experiment::cooked::ArtifactByteSource& loose,
                           const experiment::cooked::ArtifactByteSource& mounted, const LX::LXMaterialAsset& source,
                           const experiment::AssetId& graphId)
{
    using namespace material_graph;
    std::size_t checks = 0;
    const auto check = [&](bool valid, std::string message) {
        ++checks;
        if (!valid)
            throw std::runtime_error("Runtime: " + message);
    };
    std::string error;
    static_assert(std::is_same_v<decltype(GenerationStore::Prepare(
        std::declval<const GenerationPreparationRequest&>(), std::declval<const GenerationLoader&>(),
        std::declval<std::string&>())), own::shared_owner<const PreparedGeneration>>);
    static_assert(!std::is_default_constructible_v<PreparedGeneration::ConstructionKey>);
    static_assert(std::is_same_v<decltype(Instance::generation), own::shared_owner<const Generation>>);
    GenerationStore store;
    const GenerationLoader loader = [&](CookedProgram& product, std::string& failure) {
        return LoadCookedGeneration(catalog, loose, graphId, &source, product, failure);
    };
    const auto first = store.Load(graphId, loader, false, error);
    check(first && first->assetId == graphId && first->generation == 1, "GUID generation load: " + error);
    check(material_graph_test::SamePinnedObject(store.Load(graphId, loader, true, error), first), "Identical full payload retains generation identity");
    const auto changed = [&]() {
        auto graph = source;
        graph.blackboard[0].value = .234;
        return graph;
    }();
    const GenerationLoader stale = [&](CookedProgram& product, std::string& failure) {
        return LoadCookedGeneration(catalog, loose, graphId, &changed, product, failure);
    };
    check(!store.Load(graphId, stale, true, error) && material_graph_test::SamePinnedObject(store.Current(graphId), first) && !error.empty(),
          "Stale graph cannot replace the accepted generation");
    const GenerationLoader invalid = [&](CookedProgram& product, std::string&) {
        product = first->cooked;
        product.metadata.clear();
        return true;
    };
    check(!store.Load(graphId, invalid, true, error) && material_graph_test::SamePinnedObject(store.Current(graphId), first),
          "Invalid candidate retains the complete accepted generation");
    GenerationStore packaged;
    const auto pakGeneration = packaged.Load(
        graphId,
        [&](CookedProgram& product, std::string& failure) {
            return LoadCookedGeneration(catalog, mounted, graphId, nullptr, product, failure);
        },
        false, error);
    check(pakGeneration &&
              pakGeneration->cooked.product.program.semanticKey == first->cooked.product.program.semanticKey,
          "PAK generation loads by GUID without source parsing or runtime compiler");

    // A real empty CPU descriptor tests owner retention. Pixel upload and GPU
    // lifetime checks remain in the rendering probes.
    auto lifetime = own::make_shared<const Texture>();
    own::weak_owner<const Texture> weakTexture = lifetime;
    const TextureLoader textureLoader = [&lifetime](const experiment::AssetId&, LX::LXColorSpace, std::string&) {
        return lifetime;
    };
    InstanceDescription description{
        graphId, {{900, .12345678901234567}, {901, std::array<double, 4>{.2, .3, .4, 1.}}}, {}};
    own::shared_owner<const Instance> instance;
    check(BuildInstance(first, description, textureLoader, instance, error), "Pack graph instance: " + error);
    auto acceptedInstance = instance;
    {
        auto pins = own::make_shared<InstanceFramePins>();
        auto firstRepresentation = own::make_shared<const Instance>(*instance);
        auto secondRepresentation = own::make_shared<const Instance>(*instance);
        const auto firstId = firstRepresentation->representationId;
        const auto firstIndex = pins->Retain(firstRepresentation);
        check(firstIndex == pins->Retain(firstRepresentation) && pins->Size() == 1,
            "Frame pins deduplicate the exact immutable instance representation");
        check(firstId != secondRepresentation->representationId &&
            firstIndex != pins->Retain(secondRepresentation) && pins->Size() == 2,
            "Value copies have distinct representation identities within one graph generation");
        const own::weak_owner<const Instance> weak = firstRepresentation;
        const auto borrowed = pins->Borrow(firstIndex);
        firstRepresentation.reset();
        check(!weak.expired() && borrowed->representationId == firstId,
            "Frame table protects draw borrows after producer owner release");
        pins.reset();
        check(weak.expired(), "Final frame table release ends its exact instance pin");
    }
    check(instance->textures.size() == 1 && instance->textures[0].owner && !instance->uniforms.empty(),
          "CPU snapshot owns texture generation and reflected uniform values");
    auto other = description;
    other.parameters[0].value = .7;
    own::shared_owner<const Instance> secondInstance;
    check(BuildInstance(first, other, textureLoader, secondInstance, error) &&
              material_graph_test::SamePinnedObject(secondInstance->generation, instance->generation) && secondInstance->uniforms != instance->uniforms,
          "Instances share a graph and retain independent values");
    for (const auto badParameter : {ParameterOverride{900, true}, ParameterOverride{99999, .3},
                                    ParameterOverride{900, std::numeric_limits<double>::infinity()}})
    {
        auto bad = description;
        bad.parameters.push_back(badParameter);
        check(!BuildInstance(first, bad, textureLoader, instance, error) && material_graph_test::SamePinnedObject(instance, acceptedInstance),
              "Duplicate/unknown/type-invalid edits preserve snapshot");
    }
    auto badType = description;
    badType.parameters[0].value = true;
    check(!BuildInstance(first, badType, textureLoader, instance, error) && material_graph_test::SamePinnedObject(instance, acceptedInstance),
          "Mismatched scalar type rejected without duplicate ID");
    check(!BuildInstance(first, description, {}, instance, error) && material_graph_test::SamePinnedObject(instance, acceptedInstance),
          "Missing texture loader preserves all values and owners");
    auto wrongGraph = description;
    wrongGraph.graphId = {};
    check(!BuildInstance(first, wrongGraph, textureLoader, instance, error) && material_graph_test::SamePinnedObject(instance, acceptedInstance),
          "Graph identity mismatch cannot bind another generation");
    auto unknownTexture = description;
    unknownTexture.textures = {{900, instance->textures[0].assetId}};
    check(!BuildInstance(first, unknownTexture, textureLoader, instance, error) && material_graph_test::SamePinnedObject(instance, acceptedInstance),
          "Non-texture parameter cannot receive a texture override");
    experiment::AssetId replacementTexture;
    check(experiment::TryParseCanonicalAssetId("44444444-4444-4444-8444-444444444444", replacementTexture),
          "Replacement texture GUID");
    auto textureDescription = description;
    textureDescription.textures = {{905, replacementTexture}};
    own::shared_owner<const Instance> textureInstance;
    check(BuildInstance(first, textureDescription, textureLoader, textureInstance, error) &&
              textureInstance->textures[0].assetId == replacementTexture &&
              instance->textures[0].assetId != replacementTexture,
          "Public texture override changes one instance without changing the shared graph/default");
    const TextureLoader missingTexture = [](const experiment::AssetId&, LX::LXColorSpace, std::string& failure) {
        failure = "Missing replacement texture";
        return own::shared_owner<const Texture>{};
    };
    const auto priorTextureInstance = textureInstance;
    check(!BuildInstance(first, textureDescription, missingTexture, textureInstance, error) &&
              material_graph_test::SamePinnedObject(textureInstance, priorTextureInstance),
          "Failed texture reload retains all previous texture owners");

    InstanceDocument document{"LX instance", {}, true, description};
    // Every supported numeric type is archived exactly, including double
    // authoring precision; final binding still validates against the graph.
    document.description.parameters = {{904, std::array<double, 3>{1.23456789012345, -2., 3.}},
                                       {902, true},
                                       {903, std::int64_t{INT32_MIN}},
                                       {901, std::array<double, 4>{.2, .3, .4, .5}},
                                       {900, .12345678901234567}};
    document.description.textures = {{905, replacementTexture}};
    Authoring::WriteDocument saved;
    check(WriteInstanceDocument(document, saved.Root(), error), "Save typed instance document");
    const auto canonical = saved.Dump();
    const auto parsedTree = ryml::parse_in_arena(ryml::to_csubstr(canonical));
    InstanceDocument decoded;
    check(ReadInstanceDocument(Authoring::ReadNode::FromRyml(parsedTree), decoded, error),
          "Parse serialized bytes and load typed instance document");
    Authoring::WriteDocument resaved;
    check(WriteInstanceDocument(decoded, resaved.Root(), error) && resaved.Dump() == canonical,
          "Save/load/resave has identical canonical bytes");
    check(std::get<double>(decoded.description.parameters[0].value) == .12345678901234567 &&
              std::get<std::array<double, 3>>(decoded.description.parameters[4].value)[0] == 1.23456789012345,
          "Double precision values survive disk roundtrip exactly");
    check(decoded.description.textures.size() == 1 && decoded.description.textures[0].parameter == 905 &&
              decoded.description.textures[0].assetId == replacementTexture,
          "Texture parameter ID and override GUID survive serialized byte roundtrip");
    auto numericRuntime = numericProduct;
    for (auto& texture : numericRuntime.layout.textures)
    {
        texture.reference = first->cooked.product.layout.textures[0].reference;
        for (auto& resource : numericRuntime.program.resources)
            if (resource.kind == LX::LXMaterialResourceKind::Texture && resource.slot == texture.slot)
                resource.reference = texture.reference;
        for (auto& parameter : numericRuntime.program.parameters)
            if (parameter.id == texture.parameter && parameter.type == LX::PinType::Texture)
                parameter.value = texture.reference;
    }
    const GenerationLoader numeric = [&](CookedProgram& product, std::string&) {
        product.product = numericRuntime;
        product.metadata = LX::WriteMaterialProgramMetadata(numericRuntime.program);
        product.boundSource = BuildBoundSource(numericRuntime.program);
        return true;
    };
    const auto next = store.Load(graphId, numeric, true, error);
    check(next && next->generation > first->generation && material_graph_test::SamePinnedObject(instance->generation, first),
          "Reload publishes a new owner while old instances retain their previous owner");
    own::shared_owner<const Instance> numericInstance;
    check(BuildInstance(next, decoded.description, textureLoader, numericInstance, error),
          "All five typed overrides repack against actual reflection: " + error);
    auto privateProduct = numericProduct;
    privateProduct.layout.parameters[0].parameter.exposed = false;
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    auto acceptedUniforms = numericInstance->uniforms;
    const auto privateId = privateProduct.layout.parameters[0].parameter.id;
    const auto privateValue = privateProduct.layout.parameters[0].parameter.value;
    const std::array privateOverride{ParameterOverride{privateId, privateValue}};
    check(!PrepareUniforms(privateProduct.layout, privateOverride, acceptedUniforms, diagnostics) &&
              acceptedUniforms == numericInstance->uniforms,
          "Private parameters are not writable by an instance");

    for (const auto& failure :
         {"version", "unknown-key", "duplicate-key", "numeric-range", "double-value", "texture-id"})
    {
        Authoring::WriteDocument malformed;
        malformed.Root().Assign(saved.Root());
        if (std::string_view(failure) == "version")
            malformed.Root().Child("lattice_material").SetScalar(2);
        else if (std::string_view(failure) == "unknown-key")
            malformed.Root().Child("generation").SetScalar(4);
        else if (std::string_view(failure) == "duplicate-key")
            malformed.Root().Child("parameters").At(1).Child("id").SetScalar(900);
        else if (std::string_view(failure) == "numeric-range")
            malformed.Root().Child("parameters").At(3).Child("int").SetScalar(std::int64_t{INT32_MAX} + 1);
        else if (std::string_view(failure) == "double-value")
            malformed.Root().Child("parameters").At(0).Child("bool").SetScalar(true);
        else
        {
            auto texture = malformed.Root().Child("textures").Append();
            texture.Child("id").SetScalar(990);
            texture.Child("guid").SetScalar("not-a-guid");
        }
        const auto priorName = decoded.name;
        const auto priorParameters = decoded.description.parameters;
        check(!ReadInstanceDocument(malformed.Root().Read(), decoded, error) && decoded.name == priorName &&
                  decoded.description.parameters.size() == priorParameters.size() &&
                  decoded.description.parameters[0].value == priorParameters[0].value,
              "Malformed document preserves accepted description: " + std::string(failure));
    }
    store.Remove(graphId);
    check(!store.Current(graphId) && material_graph_test::SamePinnedObject(instance->generation, first) && instance->textures[0].owner,
          "Removal invalidates lookup while existing snapshots remain usable");
    const auto restored = store.Load(graphId, loader, false, error);
    check(restored && restored->generation > next->generation, "Remove/reload cannot reuse a generation number");
    lifetime.reset();
    store.Clear();
    check(!store.Current(graphId) && !weakTexture.expired(), "Cache shutdown leaves retained instance owners alive");

    GenerationStore concurrent;
    std::atomic<unsigned> loads{};
    const GenerationLoader counted = [&](CookedProgram& product, std::string&) {
        ++loads;
        product = first->cooked;
        return true;
    };
    std::vector<std::future<own::shared_owner<const Generation>>> jobs;
    for (unsigned index = 0; index < 8; ++index)
        jobs.push_back(std::async(std::launch::async, [&]() {
            std::string failure;
            return concurrent.Load(graphId, counted, false, failure);
        }));
    const auto shared = jobs.front().get();
    for (std::size_t index = 1; index < jobs.size(); ++index)
        check(material_graph_test::SamePinnedObject(jobs[index].get(), shared), "Concurrent GUID loads observe one owner");
    check(loads == 1, "Concurrent cold load executes one candidate loader");

    // Split preparation is private until the owning publication boundary. A
    // scene request and a placement request share one same-revision loader.
    GenerationStore staged;
    const GenerationLoader originalSnapshot = [first](CookedProgram& product, std::string& failure)
    {
        failure.clear();
        product = first->cooked;
        return true;
    };
    const auto accepted = staged.Load(graphId, originalSnapshot, false, error);
    check(accepted && material_graph_test::SamePinnedObject(staged.Current(graphId), accepted), "Seed split preparation owner: " + error);
    const auto cachedRequest = staged.BeginPreparation(graphId, false, error);
    const auto cachedPrepared = GenerationStore::Prepare(cachedRequest, {}, error);
    check(cachedRequest && cachedPrepared &&
              material_graph_test::SamePinnedObject(staged.Publish(*cachedPrepared, error), accepted),
          "A clean cache ticket owns its generation without a live request-state entry or loader");
    const auto replacementRequest = staged.BeginPreparation(graphId, true, error);
    const auto sharedRequest = staged.BeginPreparation(graphId, false, error);
    check(replacementRequest && sharedRequest && material_graph_test::SamePinnedObject(staged.Current(graphId), accepted),
          "Reload ticket preserves Current and allows same-revision callers to join");
    std::size_t preparedLoads = 0;
    bool readCurrentDuringPreparation = false;
    const GenerationLoader replacementSnapshot = [&](CookedProgram& product, std::string& failure)
    {
        ++preparedLoads;
        // This lookup also guards against restoring the global lock around the
        // loader: Current must remain callable while expensive work is running.
        readCurrentDuringPreparation = material_graph_test::SamePinnedObject(staged.Current(graphId), accepted);
        failure.clear();
        product = next->cooked;
        return true;
    };
    const auto replacementPrepared = GenerationStore::Prepare(replacementRequest, replacementSnapshot, error);
    const auto sharedPrepared = GenerationStore::Prepare(sharedRequest, replacementSnapshot, error);
    check(replacementPrepared && sharedPrepared && preparedLoads == 1 && readCurrentDuringPreparation &&
              material_graph_test::SamePinnedObject(staged.Current(graphId), accepted),
          "Shared preparation validates once without publishing or locking out Current: " + error);
    const auto replacementOwner = staged.Publish(*replacementPrepared, error);
    check(replacementOwner && !material_graph_test::SamePinnedObject(replacementOwner, accepted) &&
              replacementOwner->generation > accepted->generation && material_graph_test::SamePinnedObject(staged.Current(graphId), replacementOwner),
          "Owner publication atomically replaces the complete prepared generation: " + error);
    check(material_graph_test::SamePinnedObject(staged.Publish(*sharedPrepared, error), replacementOwner) &&
              material_graph_test::SamePinnedObject(staged.Load(graphId, replacementSnapshot, false, error), replacementOwner) && preparedLoads == 1,
          "Repeated publication and clean cache hits retain one owner without another loader");

    const auto invalidatedRequest = staged.BeginPreparation(graphId, true, error);
    staged.InvalidatePreparation(graphId);
    const auto invalidatedPrepared = GenerationStore::Prepare(invalidatedRequest, originalSnapshot, error);
    check(!staged.Publish(*cachedPrepared, error) && !error.empty(),
          "A superseded cached preparation cannot revive an older accepted generation");
    check(invalidatedPrepared && material_graph_test::SamePinnedObject(staged.Current(graphId), replacementOwner),
          "Invalidated work can finish privately while the accepted owner remains usable");
    check(!staged.Publish(*invalidatedPrepared, error) && !error.empty() &&
              material_graph_test::SamePinnedObject(staged.Current(graphId), replacementOwner),
          "Completion after invalidation cannot publish or replace Current");

    std::size_t failedLoads = 0;
    const GenerationLoader failedSnapshot = [&](CookedProgram&, std::string& failure)
    {
        ++failedLoads;
        failure = "Deliberate dirty preparation failure.";
        return false;
    };
    check(!staged.Load(graphId, failedSnapshot, false, error) && failedLoads == 1 && !error.empty() &&
              material_graph_test::SamePinnedObject(staged.Current(graphId), replacementOwner),
          "A dirty non-reload cannot return a stale cache hit or erase the accepted owner on failure");
    const auto retryRequest = staged.BeginPreparation(graphId, false, error);
    const auto sharedRetryRequest = staged.BeginPreparation(graphId, false, error);
    std::size_t retryLoads = 0;
    const GenerationLoader retrySnapshot = [&](CookedProgram& product, std::string& failure)
    {
        ++retryLoads;
        return originalSnapshot(product, failure);
    };
    const auto retryPrepared = GenerationStore::Prepare(retryRequest, retrySnapshot, error);
    const auto sharedRetryPrepared = GenerationStore::Prepare(sharedRetryRequest, retrySnapshot, error);
    check(retryPrepared && sharedRetryPrepared && retryLoads == 1 && material_graph_test::SamePinnedObject(staged.Current(graphId), replacementOwner),
          "Failed dirty preparation permits a fresh shared retry without early publication: " + error);
    const auto retryOwner = staged.Publish(*retryPrepared, error);
    check(retryOwner && !material_graph_test::SamePinnedObject(retryOwner, replacementOwner) && retryOwner->generation > replacementOwner->generation &&
              material_graph_test::SamePinnedObject(staged.Publish(*sharedRetryPrepared, error), retryOwner),
          "Successful dirty retry accepts one new owner: " + error);
    check(material_graph_test::SamePinnedObject(staged.Load(graphId, failedSnapshot, false, error), retryOwner) && failedLoads == 1 && error.empty(),
          "Successful publication clears dirty state and restores the cached-read fast path");

    const auto supersededRequest = staged.BeginPreparation(graphId, true, error);
    const auto latestRequest = staged.BeginPreparation(graphId, true, error);
    const auto supersededPrepared = GenerationStore::Prepare(supersededRequest, replacementSnapshot, error);
    check(supersededPrepared && !staged.Publish(*supersededPrepared, error) && !error.empty() &&
              material_graph_test::SamePinnedObject(staged.Current(graphId), retryOwner),
          "A newer request rejects an older completion even before the newer request publishes");
    const auto latestPrepared = GenerationStore::Prepare(latestRequest, originalSnapshot, error);
    check(latestPrepared && material_graph_test::SamePinnedObject(staged.Publish(*latestPrepared, error), retryOwner),
          "An unchanged latest payload keeps the accepted owner and completes its dirty revision");

    const auto removedRequest = staged.BeginPreparation(graphId, true, error);
    staged.Remove(graphId);
    const auto removedPrepared = GenerationStore::Prepare(removedRequest, replacementSnapshot, error);
    check(removedPrepared && !staged.Publish(*removedPrepared, error) && !error.empty() && !staged.Current(graphId),
          "Completion after Remove cannot revive an entry");
    const auto afterRemove = staged.Load(graphId, originalSnapshot, false, error);
    check(afterRemove && afterRemove->generation > retryOwner->generation,
          "A fresh load after Remove reserves a new generation number: " + error);
    const auto clearedRequest = staged.BeginPreparation(graphId, true, error);
    staged.Clear();
    const auto clearedPrepared = GenerationStore::Prepare(clearedRequest, replacementSnapshot, error);
    check(clearedPrepared && !staged.Publish(*clearedPrepared, error) && !error.empty() && !staged.Current(graphId),
          "Completion after Clear cannot revive an entry");
    const auto afterClear = staged.Load(graphId, originalSnapshot, false, error);
    check(afterClear && afterClear->generation > afterRemove->generation &&
              accepted->cooked.metadata == first->cooked.metadata,
          "Clear preserves retained immutable owners and never reuses a generation number: " + error);
    // Cache eviction releases only the retained cache reference, including the
    // completed preparation state that previously kept every generation alive.
    GenerationStore bounded;
    bounded.SetRetainedBudgetBytes(SIZE_MAX);
    own::weak_owner<const Generation> cachedLifetime;
    std::uint64_t cachedGeneration{};
    {
        auto resident = bounded.Load(graphId, originalSnapshot, false, error);
        check(bool(resident), "Budgeted graph generation load: " + error);
        cachedLifetime = resident;
        cachedGeneration = resident->generation;
        check(resident->RetainedPayloadBytes() > sizeof(Generation) &&
                  bounded.RetainedBytes() == resident->RetainedPayloadBytes(),
              "Graph cache charges shader/source and metadata capacities, not only its descriptor");
    }
    check(!cachedLifetime.expired(), "Unused preload survives through explicit cache retention");
    {
        auto cacheHit = bounded.Load(graphId, failedSnapshot, false, error);
        check(cacheHit && cacheHit->generation == cachedGeneration && error.empty(),
              "Retained preload does not rerun its loader");
        bounded.SetRetainedBudgetBytes(0);
        check(bounded.RetainedBudgetBytes() == 0 && bounded.RetainedBytes() == 0 &&
                  material_graph_test::SamePinnedObject(bounded.Current(graphId), cacheHit),
              "Zero retention budget drops cache bytes while a consumer still owns Current");
    }
    check(cachedLifetime.expired() && !bounded.Current(graphId),
          "Weak current and completed requests do not secretly retain an evicted graph payload");
    own::weak_owner<const Generation> preparedLifetime;
    {
        const auto request = bounded.BeginPreparation(graphId, false, error);
        const auto prepared = GenerationStore::Prepare(request, originalSnapshot, error);
        check(bool(prepared), "Prepare generation with zero retained budget: " + error);
        auto published = bounded.Publish(*prepared, error);
        check(published && published->generation > cachedGeneration && bounded.RetainedBytes() == 0,
              "A new resident generation reserves a fresh identity after eviction");
        preparedLifetime = published;
        published.reset();
        check(!preparedLifetime.expired(), "Prepared ticket pins its exact result through publication handoff");
        bounded.Clear();
        check(!bounded.Current(graphId) && !preparedLifetime.expired(),
              "Clear removes lookup but cannot revoke a prepared consumer's exact generation");
    }
    check(preparedLifetime.expired(), "No store-owned request survives its final ticket and consumer");
    std::cout << "LX_MATERIAL_RUNTIME_OK checks=" << checks << " concurrentLoads=8\n";
}
