// Unrun source fixture. Link the existing RenderEngine/Utility libraries.
// No importer, shader compiler, graphics device or payload decoder is invoked.
#include "../../Engine/RenderEngine/AssetDepot/RuntimeBootstrap.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/SceneCookProducer.h"
#include "../../Engine/Utility_Framework/ContentAbi.h"
#include "../../Engine/Utility_Framework/AuthoringParsedDocument.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
    namespace cooked = experiment::cooked;
    void Require(bool value, const char* message)
    {
        if (!value) throw std::runtime_error(message);
    }
    experiment::AssetId Id(unsigned char number)
    {
        experiment::AssetId id;
        id.value.data[6] = 0x40u;
        id.value.data[8] = 0x80u;
        id.value.data[15] = number;
        return id;
    }
    void Text(const std::filesystem::path& path, const std::string& value)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out.write(value.data(), static_cast<std::streamsize>(value.size()));
        Require(static_cast<bool>(out), "Fixture write failed");
    }
    void Verify(const std::filesystem::path& root)
    {
        std::string error, text;
        AssetDepot::RuntimeBootstrapReceipt receipt;
        receipt.assetSetHashes.push_back(std::string(64u, '1'));
        const cooked::TypedAssetReference texture{{Id(2), {}}, cooked::CookedAssetKind::Texture};
        receipt.documents.push_back({Id(1), {}, {texture}});
        Require(AssetDepot::WriteRuntimeBootstrapReceipt(receipt, text, error), "Receipt write failed");
        AssetDepot::RuntimeBootstrapReceipt decoded;
        Require(AssetDepot::ReadRuntimeBootstrapReceipt(text, decoded, error)
            && decoded.documents.front().references == receipt.documents.front().references, "Receipt parity failed");
        for (const auto& bad : {text + "set ../escape\n", text + "document malformed\n", text.substr(0, text.size() - 1),
            std::string("CEBR1\nwin-x64\nwrong-abi\n") + text.substr(text.find("legacy "))})
            Require(!AssetDepot::ReadRuntimeBootstrapReceipt(bad, decoded, error)
                && decoded.documents.size() == 1u, "Invalid receipt accepted or old parse result lost");
        auto duplicate = receipt;
        duplicate.documents.front().references.push_back(texture);
        Require(!AssetDepot::WriteRuntimeBootstrapReceipt(duplicate, text, error), "Duplicate reference accepted");
        duplicate = receipt;
        duplicate.documents.front().references.push_back({texture.key, cooked::CookedAssetKind::Mesh});
        Require(!AssetDepot::WriteRuntimeBootstrapReceipt(duplicate, text, error), "Conflicting expected kinds accepted");

        cooked::AssetSetManifest set;
        set.assetSetId = Id(3); set.revision = 1; set.targetPlatform = "win-x64"; set.targetAbi = CreatorContentAbi::Token;
        cooked::AssetBlobRecord blob;
        blob.kind = texture.kind; blob.representation = 1; blob.schemaVersion = 1; blob.byteSize = 1;
        blob.targetPlatform = set.targetPlatform; blob.targetAbi = set.targetAbi;
        blob.artifactPath = "Derived/AssetBlobs/" + std::string(64u, '0') + "/" + std::string(64u, '0') + ".png";
        set.blobs.push_back(blob); set.entries.push_back({texture, 0, {}}); set.roots.push_back(texture);
        cooked::CookedAssetCatalog empty, catalog;
        std::vector<cooked::AssetManifestIssue> issues;
        Require(empty.WithMountedAssetSet(set, own::make_shared<const cooked::LooseArtifactByteSource>(root),
            {1}, 1, {"win-x64", CreatorContentAbi::Token, {}}, catalog, issues), "Fixture mount metadata failed");
        cooked::CookedAssetManifest legacy;
        cooked::CookedAssetManifestEntry scene;
        scene.assetId = Id(1); scene.kind = cooked::CookedAssetKind::Scene; scene.formatVersion = cooked::kSceneArtifactVersion;
        scene.byteSize = 1; scene.artifactPath = cooked::MakeDerivedSceneArtifactPath(scene.assetId);
        legacy.entries.push_back(scene); legacy.sourceAssets.push_back({scene.assetId, "Scenes/Start.creator"});
        const auto legacyBytes = cooked::WriteAssetManifest(legacy);
        Require(legacyBytes.Succeeded(), "Legacy document fixture rejected");
        Text(root / "Derived/asset-manifest.cemf", {reinterpret_cast<const char*>(legacyBytes.bytes.data()), legacyBytes.bytes.size()});
        Require(cooked::ComputeSha256(legacyBytes.bytes, receipt.legacyManifestSha256, error), "Legacy hash failed");
        Require(AssetDepot::WriteRuntimeBootstrapReceipt(receipt, text, error), "Receipt write failed");
        Text(root / "Derived/bootstrap-asset-references.cebr", text);
        // Neither Scene nor Texture payload exists: startup validation is metadata-only.
        Require(AssetDepot::ValidateRuntimeBootstrap(root, receipt.assetSetHashes, catalog, error), "Valid bootstrap rejected");
        Require(!AssetDepot::ValidateRuntimeBootstrap(root, {std::string(64u, '2')}, catalog, error), "Changed exact AssetSet group accepted");
        Require(!AssetDepot::ValidateRuntimeBootstrap(root, receipt.assetSetHashes, empty, error), "Missing external reference accepted");
        auto wrong = receipt;
        wrong.documents.front().references.front().kind = cooked::CookedAssetKind::Mesh;
        Require(AssetDepot::WriteRuntimeBootstrapReceipt(wrong, text, error), "Wrong-kind fixture encode failed");
        Text(root / "Derived/bootstrap-asset-references.cebr", text);
        Require(!AssetDepot::ValidateRuntimeBootstrap(root, receipt.assetSetHashes, catalog, error), "Wrong-kind reference accepted");
        wrong = receipt; wrong.documents.front().contentSha256[0] = 1;
        Require(AssetDepot::WriteRuntimeBootstrapReceipt(wrong, text, error), "Changed-document fixture encode failed");
        Text(root / "Derived/bootstrap-asset-references.cebr", text);
        Require(!AssetDepot::ValidateRuntimeBootstrap(root, receipt.assetSetHashes, catalog, error), "Changed document identity accepted");

        const auto source = root / "Source/Scenes/Bootstrap.creator";
        Text(source, "m_meshAssetId: " + Uuid::ToString(Id(4).value) + "\nm_modelGuid: " + Uuid::ToString(Id(5).value) + "\n");
        Text(source.string() + ".meta", "guid: " + Uuid::ToString(Id(1).value) + "\n");
        const auto product = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(product.Succeeded() && product.product->bootstrapReferences.size() == 1u
            && product.product->bootstrapReferences.front().kind == cooked::CookedAssetKind::Mesh,
            "Direct Mesh document required unrelated source Model");
        const auto old = cooked::BuildSceneCookProduct({source, root / "Source"});
        Require(old.Succeeded() && old.product->bootstrapReferences.empty()
            && old.product->manifestEntry.dependencies == std::vector<experiment::AssetId>{Id(5)}, "Legacy document semantics changed");

        const auto materialText = "ref: " + Uuid::ToString(Id(6).value)
            + "\noverrides:\n  - name: colorMap\n    texture: {guid: " + Uuid::ToString(Id(2).value) + ", colorSpace: srgb}\n";
        const auto has = [](const cooked::SceneCookProduct& value, experiment::AssetId id, cooked::CookedAssetKind kind)
        {
            return std::ranges::find(value.bootstrapReferences, cooked::TypedAssetReference{{id, {}}, kind})
                != value.bootstrapReferences.end();
        };
        const auto rejectMissingBase = [&](const cooked::SceneCookProduct& value)
        {
            cooked::CookedAssetManifest documentOnly;
            auto entry = value.manifestEntry;
            entry.dependencies.clear(); // The producer's explicit CEBR bridge removes checked external edges only.
            documentOnly.entries.push_back(entry);
            documentOnly.sourceAssets.push_back({value.sceneAssetId, "Scenes/Bootstrap.creator"});
            const auto encoded = cooked::WriteAssetManifest(documentOnly);
            Require(encoded.Succeeded(), "Reference document manifest fixture failed");
            Text(root / "Derived/asset-manifest.cemf", {reinterpret_cast<const char*>(encoded.bytes.data()), encoded.bytes.size()});
            auto captured = receipt;
            Require(cooked::ComputeSha256(encoded.bytes, captured.legacyManifestSha256, error), "Reference document hash failed");
            captured.documents = {{value.sceneAssetId, value.manifestEntry.contentSha256, value.bootstrapReferences}};
            Require(AssetDepot::WriteRuntimeBootstrapReceipt(captured, text, error), "Reference receipt write failed");
            Text(root / "Derived/bootstrap-asset-references.cebr", text);
            Require(!AssetDepot::ValidateRuntimeBootstrap(root, captured.assetSetHashes, catalog, error),
                "Missing Material base was accepted with a present override Texture");
            auto materialSet = set;
            materialSet.entries.front().asset = {{Id(6), {}}, cooked::CookedAssetKind::Material};
            materialSet.roots = {materialSet.entries.front().asset};
            materialSet.blobs.front().kind = cooked::CookedAssetKind::Material;
            materialSet.blobs.front().artifactPath = "Derived/AssetBlobs/" + std::string(64u, '0') + "/" + std::string(64u, '0') + ".asset";
            cooked::CookedAssetCatalog materialOnly;
            Require(empty.WithMountedAssetSet(materialSet, own::make_shared<const cooked::LooseArtifactByteSource>(root),
                {1}, 1, {"win-x64", CreatorContentAbi::Token, {}}, materialOnly, issues), "Material fixture metadata mount failed");
            Require(!AssetDepot::ValidateRuntimeBootstrap(root, captured.assetSetHashes, materialOnly, error),
                "Missing override Texture was accepted with a present base Material");
        };
        std::string indented;
        for (std::istringstream lines(materialText); lines;)
        {
            std::string line;
            if (std::getline(lines, line)) indented += "  " + line + "\n";
        }
        Text(source, "m_Material:\n" + indented);
        const auto referenced = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(referenced.Succeeded() && referenced.product->bootstrapReferences.size() == 2u
            && has(*referenced.product, Id(6), cooked::CookedAssetKind::Material)
            && has(*referenced.product, Id(2), cooked::CookedAssetKind::Texture), "Material reference or typed override Texture was omitted");
        rejectMissingBase(*referenced.product);
        const auto legacyReference = cooked::BuildSceneCookProduct({source, root / "Source"});
        Require(legacyReference.Succeeded() && legacyReference.product->manifestEntry.dependencies.empty(),
            "Legacy Material-reference extraction changed");
        Text(source, "m_propertyName: m_Material\nm_valueYaml: |\n" + indented);
        const auto overridden = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(overridden.Succeeded() && overridden.product->bootstrapReferences == referenced.product->bootstrapReferences,
            "Prefab material override lost its material context");
        rejectMissingBase(*overridden.product);
        Text(source, "arbitrary_metadata:\n" + indented);
        const auto unrelated = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(unrelated.Succeeded() && unrelated.product->bootstrapReferences.empty(),
            "An unrelated ref field was reinterpreted as a Material dependency");
        Text(source, "m_Material: {schema: 1, assetId: " + Uuid::ToString(Id(6).value)
            + ", shaderAssetId: " + Uuid::ToString(Id(7).value)
            + ", name: Cloth, blendMode: opaque, properties: [], keywords: [], keywordSelections: []}\n");
        const auto inlineMaterial = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(inlineMaterial.Succeeded() && has(*inlineMaterial.product, Id(6), cooked::CookedAssetKind::Material)
            && has(*inlineMaterial.product, Id(7), cooked::CookedAssetKind::ShaderMeta),
            "Inline authored Material identity needed for CodeProgram was omitted");
        Text(source, "m_Material: {schema: 1, assetId: 00000000-0000-0000-0000-000000000000, shaderAssetId: "
            + Uuid::ToString(Id(7).value)
            + ", name: Inline, blendMode: opaque, properties: [], keywords: [], keywordSelections: []}\n");
        const auto unboundInline = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(!unboundInline.Succeeded() && !unboundInline.issues.empty()
            && unboundInline.issues.front().context == "scene.bootstrapMaterialBinding",
            "Nil inline Material identity was accepted without a source-free Code program binding");
        Require(cooked::BuildSceneCookProduct({source, root / "Source"}).Succeeded(),
            "Bootstrap-only inline Material restriction changed legacy mode");

        const auto foliageType = "{m_modelGuid: " + Uuid::ToString(Id(5).value)
            + ", m_meshAssetId: " + Uuid::ToString(Id(4).value)
            + ", m_materialAssetId: " + Uuid::ToString(Id(6).value) + ", m_allowLegacySource: false}";
        Text(source, "m_foliageTypes: [" + foliageType + "]\n");
        const auto inlineFoliage = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(inlineFoliage.Succeeded() && inlineFoliage.product->bootstrapReferences.size() == 3u
            && has(*inlineFoliage.product, Id(5), cooked::CookedAssetKind::Model)
            && has(*inlineFoliage.product, Id(4), cooked::CookedAssetKind::Mesh)
            && has(*inlineFoliage.product, Id(6), cooked::CookedAssetKind::Material),
            "Inline Foliage lost its parent Model or explicit Material");
        const auto legacyFoliage = cooked::BuildSceneCookProduct({source, root / "Source"});
        Require(legacyFoliage.Succeeded() && legacyFoliage.product->manifestEntry.dependencies == std::vector<experiment::AssetId>{Id(5)},
            "Bootstrap Foliage extraction changed legacy scene dependencies");
        Text(source, "user_metadata: {m_allowLegacySource: true, label: example}\n");
        const auto userFlag = cooked::BuildSceneCookProduct({source, root / "Source", true});
        Require(userFlag.Succeeded() && userFlag.product->bootstrapReferences.empty(),
            "An unrelated legacy-source flag was interpreted as Foliage");

        const auto foliageSource = root / "Source/Foliage/Trees.foliage";
        Text(foliageSource, "FoliageAsset: {Types: [" + foliageType + "], Instances: []}\n");
        const auto foliageDocument = Authoring::ParsedDocument::ParseFile(foliageSource.string(), error);
        std::vector<cooked::TypedAssetReference> foliageReferences;
        Require(foliageDocument && cooked::CollectFoliageBootstrapReferences(foliageDocument.Root(), foliageReferences, error)
            && foliageReferences == inlineFoliage.product->bootstrapReferences,
            "Preserved Foliage document did not use the same typed bootstrap boundary");
        const auto allFound = [&](const cooked::CookedAssetCatalog& mounted)
        {
            for (const auto& reference : foliageReferences)
            {
                cooked::ResolvedAssetEntry resolved;
                if (mounted.Find(reference, resolved) != cooked::AssetLookupStatus::Found) return false;
            }
            return true;
        };
        const auto foliageCatalog = [&](int omitted, bool wrongKind)
        {
            auto selected = set;
            selected.entries.clear(); selected.blobs.clear(); selected.roots.clear();
            for (std::size_t index = 0u; index != foliageReferences.size(); ++index)
            {
                if (static_cast<int>(index) == omitted) continue;
                auto reference = foliageReferences[index];
                if (wrongKind && index == 0u) reference.kind = cooked::CookedAssetKind::Texture;
                auto payload = blob;
                payload.kind = reference.kind;
                const auto extension = reference.kind == cooked::CookedAssetKind::Model ? ".cemd"
                    : reference.kind == cooked::CookedAssetKind::Mesh ? ".cege"
                    : reference.kind == cooked::CookedAssetKind::Material ? ".asset" : ".png";
                payload.artifactPath = "Derived/AssetBlobs/" + std::string(64u, '0') + "/" + std::string(64u, '0') + extension;
                selected.entries.push_back({reference, static_cast<std::uint32_t>(selected.blobs.size()), {}});
                selected.blobs.push_back(payload); selected.roots.push_back(reference);
            }
            cooked::CookedAssetCatalog result;
            Require(empty.WithMountedAssetSet(selected, own::make_shared<const cooked::LooseArtifactByteSource>(root),
                {1}, 1, {"win-x64", CreatorContentAbi::Token, {}}, result, issues), "Foliage reference fixture catalog failed");
            return result;
        };
        Require(allFound(foliageCatalog(-1, false)), "Complete Foliage typed union failed");
        for (int omitted = 0; omitted != 3; ++omitted)
            Require(!allFound(foliageCatalog(omitted, false)), "Missing explicit Foliage identity passed preflight");
        Require(!allFound(foliageCatalog(-1, true)), "Wrong-kind Foliage identity passed preflight");
        Text(foliageSource, "FoliageAsset: {Types: [{m_modelGuid: " + Uuid::ToString(Id(5).value)
            + ", m_meshAssetId: 00000000-0000-0000-0000-000000000000, m_materialAssetId: 00000000-0000-0000-0000-000000000000}], Instances: []}\n");
        const auto defaultChildren = Authoring::ParsedDocument::ParseFile(foliageSource.string(), error);
        Require(defaultChildren && cooked::CollectFoliageBootstrapReferences(defaultChildren.Root(), foliageReferences, error)
            && foliageReferences == std::vector<cooked::TypedAssetReference>{{{Id(5), {}}, cooked::CookedAssetKind::Model}},
            "Optional Foliage child identities could not use their model descriptor defaults");
        for (const auto& invalid : {std::string("FoliageAsset: {Types: [{m_allowLegacySource: true, m_modelName: Tree}], Instances: []}\n"),
            std::string("FoliageAsset: {Types: [broken], Instances: []}\n"),
            std::string("FoliageAsset: {Types: {m_modelName: Tree}, Instances: []}\n")})
        {
            Text(foliageSource, invalid);
            const auto rejected = Authoring::ParsedDocument::ParseFile(foliageSource.string(), error);
            Require(rejected && !cooked::CollectFoliageBootstrapReferences(rejected.Root(), foliageReferences, error),
                "Source-only or malformed Foliage type passed bootstrap preflight");
        }



    }
}

int main()
{
    const auto root = std::filesystem::temp_directory_path() / ("runtime-bootstrap-"
        + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        Verify(root);
        std::filesystem::remove_all(root);
        std::cout << "RUNTIME_BOOTSTRAP_OK\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
