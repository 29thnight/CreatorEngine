#include "../../Engine/RenderEngine/DataSystem.h"
#include "../../Engine/RenderEngine/Material.h"
#include "../../Engine/Utility_Framework/AuthoringNodeViewAccess.h"
#include "../../Engine/Utility_Framework/AuthoringParsedDocument.h"
#include "../../Engine/Utility_Framework/AuthoringWriteNode.h"
#include "../../Engine/Utility_Framework/PathFinder.h"
#include "../../Engine/Utility_Framework/ReflectionTypedYml.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

struct ComApartment
{
    ComApartment()
    {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)))
            throw std::runtime_error("WIC host COM initialization failed.");
    }
    ~ComApartment() { CoUninitialize(); }
};

int main(int argc, char** argv)
{
    try
    {
        ComApartment apartment;
        Meta::Register<Material>();
        Meta::Typed::RegisterOps<Material>();
        if (argc != 2)
            throw std::runtime_error("Expected packaged fixture Assets root.");
        const auto root = std::filesystem::absolute(argv[1]);
        auto* paths = InternalPath::GetInstance();
        paths->DataPath = root;
        paths->BaseProjectPath = root.parent_path();
        paths->AssetAuthoringEnabled = false;
        auto* data = DataSystem::GetInstance();
        data->Initialize();
        std::size_t checks = 0;
        const auto check = [&](bool valid, const std::string& message) {
            ++checks;
            if (!valid)
                throw std::runtime_error(message);
        };
        FileGuid graph;
        check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graph.m_guid), "Fixture graph GUID");
        std::string error;
        const auto generation = data->LoadMaterialGraphGeneration(graph, error);
        check(!!generation, "Actual DataSystem graph GUID load: " + error);
        material_graph::InstanceDescription description{experiment::AssetId{graph.m_guid}, {{900, .31}}, {}};
        Material material;
        material.m_name = "DataSystem LX instance";
        check(data->ConfigureMaterialGraph(material, description, error), "Actual instance/Texture load: " + error);
        const auto original = material.GetMaterialGraphInstance();
        check(original && original->generation == generation && original->textures.size() == 1 &&
                  original->textures[0].owner && original->textures[0].owner->GetImageView().Width() == 1,
              "Actual decoded texture is owned by the graph instance");
        const auto format = original->textures[0].owner->GetImageView().Format();
        check(format == RHIFormat::RGBA8UnormSrgb || format == RHIFormat::BGRA8UnormSrgb,
              "Actual CPU texture storage preserves graph SRGB intent");
        check(material.TrySetMaterialGraphParameter(900, .47, error) &&
                  material.GetMaterialGraphInstance() != original &&
                  material.GetMaterialGraphInstance()->uniforms != original->uniforms,
              "Public numeric setter publishes a new owning instance snapshot");
        Material clone(material);
        const auto sharedInstance = clone.GetMaterialGraphInstance();
        data->FinalizeMaterialRuntime(clone);
        check(clone.GetMaterialGraphInstance() == sharedInstance,
              "Legacy scene clone finalization preserves the accepted LX snapshot");
        check(clone.TrySetMaterialGraphParameter(900, .82, error) &&
                  clone.GetMaterialGraphInstance()->generation == generation &&
                  material.GetMaterialGraphInstance() == sharedInstance,
              "Material clone overrides do not mutate the original");
        Authoring::WriteDocument saved;
        check(data->SerializeMaterialPayload(material, saved.Root()), "Actual DataSystem authoring save");
        const auto parsed = Authoring::ParsedDocument::ParseText(saved.Dump(), error);
        check(!!parsed, "Actual byte parse: " + error);
        const auto parsedRoot = parsed.Root();
        Material restored;
        check(data->DeserializeMaterialPayload(restored, Authoring::NodeViewAccess::Make(parsedRoot)) &&
                  restored.GetMaterialGraphInstance()->uniforms == material.GetMaterialGraphInstance()->uniforms,
              "Actual authoring save/reopen reproduces packed instance values");
        Authoring::WriteDocument resaved;
        check(data->SerializeMaterialPayload(restored, resaved.Root()) && resaved.Dump() == saved.Dump(),
              "Actual DataSystem canonical resave bytes match");
        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        check(data->SerializeMaterialBinaryPayload(material, binary), "Actual CEMT/CEDO save");
        Material binaryRestored;
        check(data->DeserializeMaterialBinaryPayload(binaryRestored, binary) &&
                  binaryRestored.GetMaterialGraphInstance()->uniforms == material.GetMaterialGraphInstance()->uniforms,
              "Actual CEMT/CEDO load repacks typed overrides");
        const auto accepted = restored.GetMaterialGraphInstance();
        saved.Root().Child("lattice_material").SetScalar(99);
        const auto malformedRoot = saved.Root().Read();
        check(!data->DeserializeMaterialPayload(restored, Authoring::NodeViewAccess::Make(malformedRoot)) &&
                  restored.GetMaterialGraphInstance() == accepted,
              "Bad authoring document preserves the accepted Material snapshot");
        auto missingTexture = description;
        experiment::AssetId absent;
        check(experiment::TryParseCanonicalAssetId("44444444-4444-4444-8444-444444444444", absent),
              "Absent texture GUID");
        missingTexture.textures = {{905, absent}};
        check(!data->ConfigureMaterialGraph(restored, missingTexture, error) &&
                  restored.GetMaterialGraphInstance() == accepted,
              "Packaged texture override outside CEMF fails without losing accepted owners");
        auto badBinary = binary.str();
        badBinary.resize(15);
        std::istringstream truncated(badBinary, std::ios::binary);
        check(!data->DeserializeMaterialBinaryPayload(restored, truncated) &&
                  restored.GetMaterialGraphInstance() == accepted,
              "Truncated CEMT/CEDO preserves the complete previous instance");
        Material legacy;
        legacy.m_name = "Legacy material";
        Authoring::WriteDocument legacyDocument;
        check(data->SerializeMaterialPayload(legacy, legacyDocument.Root()), "Legacy material writer remains usable");
        const auto legacyRoot = legacyDocument.Root().Read();
        Material replaced(restored);
        check(data->DeserializeMaterialPayload(replaced, Authoring::NodeViewAccess::Make(legacyRoot)) &&
                  !replaced.HasMaterialGraph() && replaced.m_name == legacy.m_name,
              "Explicit legacy document replacement clears LX state after successful decode");

        // Switch only the authoring capability; the mounted CEMF registry already
        // points at copied sources in this isolated fixture package.
        paths->AssetAuthoringEnabled = true;
        const auto sourcePath = data->GetFilePath(graph);
        const auto source = LX::LXMaterialAsset::Load(sourcePath, LX::CreateMaterialDefinitions(), &error);
        check(!!source, "Editor reload source: " + error);
        auto changed = *source;
        changed.blackboard[0].value = .23;
        check(changed.Save(sourcePath, &error), "Stage changed source");
        const RuntimeAssetChange change{RuntimeAssetChangeKind::ContentReload, RuntimeAssetType::Auto, graph,
                                        sourcePath};
        const bool rejected =
            !data->ApplyAssetChange(change) && data->ResolveMaterialGraphGeneration(graph) == generation;
        const bool sourceRestored = source->Save(sourcePath, &error);
        check(rejected && sourceRestored && restored.GetMaterialGraphInstance() == accepted,
              "Actual Auto asset-change reload rejects stale graph and preserves cache/Material");
        check(data->ApplyAssetChange(change) && data->ResolveMaterialGraphGeneration(graph) == generation,
              "Restored identical source reload retains its existing generation");
        check(data->ApplyAssetChange({RuntimeAssetChangeKind::Removed, RuntimeAssetType::Auto, graph, sourcePath}) &&
                  !data->ResolveMaterialGraphGeneration(graph) && accepted->generation == generation,
              "Actual removal retires GUID lookup while retained instances own the program");
        data->Finalize();
        check(accepted->textures[0].owner && accepted->textures[0].owner->GetImageView().Width() == 1,
              "DataSystem finalization does not destroy externally owned instance textures");
        std::cout << "LX_MATERIAL_DATASYSTEM_OK checks=" << checks << "\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "LX_MATERIAL_DATASYSTEM_FAILED " << exception.what() << '\n';
        return 1;
    }
}
