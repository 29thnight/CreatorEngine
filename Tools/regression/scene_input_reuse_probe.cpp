// The runner extracts unchanged production operations. These small surrounding
// draw/material records do not stand in for native sealing or GPU integration.
#include "Engine/Utility_Framework/Foundation/EngineOrderedMap.h"
#include "Engine/RenderEngine/Assets/ModelGeometryPayload.h"
#include "SceneInputMeshView.inl"

namespace material_graph
{
    struct Generation
    {
        experiment::AssetId assetId;
        std::uint64_t generation{};
    };
    struct Instance
    {
        own::shared_owner<const Generation> generation;
        std::uint64_t representationId{};
    };
#include "SceneInputPins.inl"
}

struct Mesh
{
    struct
    {
        std::size_t m_ID_Data{};
    } m_hashingMesh;
};

struct EnhancedDrawItem
{
    std::size_t geometryKey{};
    RHIModelMeshView modelMeshView;
    std::size_t modelPinIndex = SIZE_MAX;
    Mesh* mesh{};
};

namespace enhanced_draw
{
#include "SceneInputIdentity.inl"
}

namespace material_graph
{
    struct SceneDrawInput
    {
        std::size_t sourceIndex{}, geometryKey{};
        assets::ModelMeshHandle mesh;
        std::size_t modelPinIndex = SIZE_MAX;
        own::local_view<const Instance> material;
        std::size_t materialPinIndex = InstanceFramePins::InvalidIndex;
    };
#include "SceneInputComparisons.inl"

    struct BorrowProbe
    {
        ce::dynamic_array<SceneDrawInput> draws_;
        own::shared_owner<InstanceFramePins> materialPins_;
#include "SceneInputBorrow.inl"
    };

    bool Fail(std::string& error, std::string message)
    {
        error = std::move(message);
        return false;
    }

    bool ModelHint(const EnhancedDrawItem& draw, const assets::ModelAssetGenerationPins& models,
                   SceneDrawInput& input, std::string& error)
    {
        struct Candidate
        {
            const assets::ModelAssetGenerationPins* modelPins_;
        } value{&models};
        const auto* candidate = &value;
        const auto& geometry = draw.modelMeshView;
        input.mesh = geometry.handle;
#include "SceneInputModelHint.inl"
        input.modelPinIndex = static_cast<std::size_t>(pin - pins.begin());
        return true;
    }
}

namespace
{
    unsigned checks{};
    void Check(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
        ++checks;
    }

    uuid::uuid16 Id(std::uint8_t value)
    {
        uuid::uuid16 id;
        id.data_[0] = value;
        id.data_[6] = 0x80;
        id.data_[8] = 0x80;
        return id;
    }

    assets::ModelMeshHandle Handle(std::uint8_t value)
    {
        assets::ModelMeshHandle result;
        result.meshId = Id(value);
        result.domain = assets::ModelMeshDomain::Granular;
        result.asset.key.assetId.value = result.meshId;
        result.asset.kind = experiment::cooked::CookedAssetKind::Mesh;
        experiment::cooked::AssetBlobRecord blob;
        blob.kind = experiment::cooked::CookedAssetKind::Mesh;
        blob.byteSize = 100;
        blob.representation = blob.schemaVersion = 1;
        blob.targetPlatform = "portable-contract";
        blob.targetAbi = "fixture";
        blob.artifactPath = "Derived/fixture.mesh";
        result.blob = own::make_shared<const experiment::cooked::AssetBlobRecord>(std::move(blob));
        result.resolverRevision = result.mountId = result.manifestRevision = 1;
        result.assetSetId.value = Id(100);
        return result;
    }
}

int main()
{
    using namespace material_graph;
    auto generation = own::make_shared<Generation>();
    generation->assetId.value = Id(3);
    generation->generation = 1;
    auto first = own::make_shared<const Instance>(Instance{generation, 1});
    auto second = own::make_shared<const Instance>(Instance{generation, 2});
    auto pins = own::make_shared<InstanceFramePins>();
    const auto firstIndex = pins->Retain(first);
    const auto secondIndex = pins->Retain(second);
    for (auto index : {firstIndex, secondIndex, InstanceFramePins::InvalidIndex})
    {
        const auto borrowed = pins->BorrowExact(*first, index);
        Check(borrowed && &*borrowed == &*first && index == firstIndex, "Exact hint or lookup fallback failed");
    }
    auto impostor = own::make_shared<const Instance>(Instance{generation, 1});
    auto index = firstIndex;
    Check(!pins->BorrowExact(*impostor, index) && index == InstanceFramePins::InvalidIndex,
          "Equal identity values bypassed the exact owner check");
    auto nextGeneration = own::make_shared<Generation>(*generation);
    ++nextGeneration->generation;
    auto replacement = own::make_shared<const Instance>(Instance{nextGeneration, 1});
    index = firstIndex;
    Check(!pins->BorrowExact(*replacement, index), "Old pin admitted a new graph generation");
    const auto replacementIndex = pins->Retain(replacement);
    index = firstIndex;
    const auto replacementBorrow = pins->BorrowExact(*replacement, index);
    Check(replacementBorrow && &*replacementBorrow == &*replacement && index == replacementIndex,
          "Retained replacement did not resolve independently");

    BorrowProbe input;
    input.materialPins_ = pins;
    input.draws_.push_back({0, 0, {}, SIZE_MAX, pins->Borrow(firstIndex), firstIndex});
    const auto& sealed = input.draws_.front();
    Check(&*input.MaterialBorrow(sealed) == &*first, "Sealed material borrow changed owner");
    auto external = sealed;
    external.material = second.borrow();
    Check(&*input.MaterialBorrow(external) == &*first, "External record bypassed indexed lookup");
    std::thread append([&]
    {
        for (std::uint64_t serial = 3; serial < 1024; ++serial)
        {
            pins->Retain(own::make_shared<const Instance>(Instance{generation, serial}));
        }
    });
    for (unsigned iteration = 0; iteration < 2048; ++iteration)
    {
        Check(&*input.MaterialBorrow(sealed) == &*first, "Appending pins invalidated a sealed borrow");
    }
    append.join();

    std::array<EnhancedDrawItem, 2> draws;
    std::array<SceneDrawInput, 2> prior;
    for (unsigned ordinal = 0; ordinal < draws.size(); ++ordinal)
    {
        draws[ordinal].geometryKey = ordinal + 1;
        draws[ordinal].modelMeshView.handle = Handle(ordinal + 1);
        prior[ordinal].geometryKey = draws[ordinal].geometryKey;
        prior[ordinal].mesh = draws[ordinal].modelMeshView.handle;
    }
    std::string error;
    Check(enhanced_draw::ValidateGeometryIdentities(draws, error), "Initial identities collide");
    Check(MatchesGeometryIdentityOrder(draws, prior), "Exact ordered identities did not match");
    draws[0].modelMeshView.handle.blob =
        own::make_shared<const experiment::cooked::AssetBlobRecord>(prior[0].mesh.Blob());
    Check(MatchesGeometryIdentityOrder(draws, prior), "Equal blob values require the same allocation");
    auto descriptor = own::make_shared<assets::ModelMeshDescriptor>();
    descriptor->meshId = prior[0].mesh.meshId;
    descriptor->origin.entry.asset = prior[0].mesh.asset;
    descriptor->origin.blob = prior[0].mesh.Blob();
    descriptor->origin.resolverRevision = prior[0].mesh.resolverRevision;
    descriptor->origin.mountId.value = prior[0].mesh.mountId;
    descriptor->origin.assetSetId = prior[0].mesh.assetSetId;
    descriptor->origin.manifestRevision = prior[0].mesh.manifestRevision;
    auto foreignDescriptor = own::make_shared<assets::ModelMeshDescriptor>(*descriptor);
    ++foreignDescriptor->origin.resolverRevision;
    assets::ModelAssetGenerationPins models;
    models.meshes.push_back(descriptor);
    models.meshes.insert(models.meshes.begin(), foreignDescriptor);
    draws[0].modelMeshView.sourceDescriptor = &*descriptor;
    SceneDrawInput resolved;
    for (const auto hint : {std::size_t{0}, std::size_t{1}, SIZE_MAX})
    {
        draws[0].modelPinIndex = hint;
        Check(ModelHint(draws[0], models, resolved, error) && resolved.modelPinIndex == 1,
              "Model hint failed exact owner fallback");
    }
    auto sameValues = own::make_shared<assets::ModelMeshDescriptor>(*descriptor);
    draws[0].modelMeshView.sourceDescriptor = &*sameValues;
    Check(!ModelHint(draws[0], models, resolved, error), "Equal descriptor values bypassed owner provenance");
    draws[0].modelMeshView.sourceDescriptor = &*descriptor;
    for (unsigned field = 0; field < 19; ++field)
    {
        auto& handle = draws[0].modelMeshView.handle;
        handle = prior[0].mesh;
        auto blob = own::make_shared<experiment::cooked::AssetBlobRecord>(handle.Blob());
        switch (field)
        {
        case 0:
            handle.modelId = Id(9);
            break;
        case 1:
            handle.meshId = Id(9);
            break;
        case 2:
            ++handle.generation;
            break;
        case 3:
            handle.domain = assets::ModelMeshDomain::LegacyAggregate;
            break;
        case 4:
            handle.asset.key.assetId.value = Id(9);
            break;
        case 5:
            handle.asset.key.subassetId.value = Id(9);
            break;
        case 6:
            handle.asset.kind = experiment::cooked::CookedAssetKind::Model;
            break;
        case 7:
            blob->contentSha256[0] ^= 1;
            break;
        case 8:
            ++blob->byteSize;
            break;
        case 9:
            blob->kind = experiment::cooked::CookedAssetKind::Model;
            break;
        case 10:
            ++blob->representation;
            break;
        case 11:
            ++blob->schemaVersion;
            break;
        case 12:
            blob->targetPlatform += "-other";
            break;
        case 13:
            blob->targetAbi += "-other";
            break;
        case 14:
            blob->artifactPath += ".other";
            break;
        case 15:
            ++handle.resolverRevision;
            break;
        case 16:
            ++handle.mountId;
            break;
        case 17:
            handle.assetSetId.value = Id(9);
            break;
        case 18:
            ++handle.manifestRevision;
            break;
        }
        handle.blob = std::move(blob);
        Check(!MatchesGeometryIdentityOrder(draws, prior), "Changed complete identity reused the old proof");
        Check(!MatchesMeshDescriptor(handle, *descriptor), "Changed complete identity matched the descriptor");
        Check(!ModelHint(draws[0], models, resolved, error), "A pin hint bypassed changed descriptor identity");
    }
    draws[0].modelMeshView.handle = prior[0].mesh;
    std::swap(draws[0], draws[1]);
    Check(!MatchesGeometryIdentityOrder(draws, prior), "Reordering reused ordered proof");
    Check(enhanced_draw::ValidateGeometryIdentities(draws, error), "Reordered valid identities were rejected");
    draws[1].geometryKey = draws[0].geometryKey;
    Check(!MatchesGeometryIdentityOrder(draws, prior) && !enhanced_draw::ValidateGeometryIdentities(draws, error),
          "New compact-key collision reused old proof or bypassed validation");
    Check(!MatchesGeometryIdentityOrder(std::span(draws).first(1), prior), "Changed draw count reused proof");
    std::cout << "SCENE_INPUT_REUSE_OK checks=" << checks
              << " fullIdentityFields=19 concurrentAppends=1021 nativeSeal=not-run gpu=not-run\n";
}
