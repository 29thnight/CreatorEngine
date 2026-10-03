#include "DataSystem.h"
#include "Material.h"
#include "MaterialGraphShaderMeta.h"
#include "AuthoringNodeViewAccess.h"
#include "AuthoringParsedDocument.h"
#include "AuthoringParseTelemetry.h"
#include "AuthoringWriteNode.h"
#include "PathFinder.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

void RunMaterialCodeRuntimeTests(const std::filesystem::path& root);
void RunCookedGraphicsIdentityTests(const material_graph::VerifiedProduct& product);
void RunMaterialPipelineRuntimeTests(const std::filesystem::path& repository);

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--pipeline-runtime")
        {
            const auto repository = std::filesystem::absolute(argv[2]);
            auto* paths = InternalPath::GetInstance();
            paths->ShaderSourcePath = repository / "Dynamic_CPP/Assets/Shaders";
            paths->BaseProjectPath = repository / "Dynamic_CPP";
            paths->AssetAuthoringEnabled = true;
            RunMaterialPipelineRuntimeTests(repository);
            return 0;
        }
        if (argc != 4) throw std::runtime_error("Expected source-free packaged Assets root, repository and authoring fixture");
        const auto root = std::filesystem::absolute(argv[1]);
        auto* paths = InternalPath::GetInstance();
        paths->DataPath = root;
        paths->BaseProjectPath = root.parent_path();
        paths->AssetAuthoringEnabled = false;
        const auto parserCallsBeforePackage = Authoring::GetTextParseTelemetry().calls;
        auto* data = DataSystem::GetInstance();
        data->Initialize();
        std::size_t checks{};
        const auto check = [&](bool valid, std::string message) {
            ++checks;
            if (!valid) throw std::runtime_error(std::move(message));
        };
        const FileGuid guid{"11111111-1111-4111-8111-111111111111"};
        std::string error;
        const auto generation = data->LoadMaterialGraphGeneration(guid, error);
        check(generation && generation->cooked.product.materialShader, "DataSystem cooked common shader load: " + error);
        check(!std::filesystem::exists(root / "fixture.shadergraph"), "No authoring graph in package");
        const auto& shader = *generation->cooked.product.materialShader;
        check(!std::filesystem::exists(shader.meta.ResolveSource(shader.meta.originPath)), "No generated source file required");
        Material material;
        material.m_name = "Generated common material";
        material_graph::InstanceDescription description{experiment::AssetId{guid.m_guid}, {{900, .31}}, {}};
        check(data->ConfigureMaterialGraph(material, description, error), "Common instance configuration: " + error);
        check(material.GetGeneratedShaderMeta() == &shader.meta && material.GetShaderBindingLayout() == &shader.layout &&
            material.GetShaderPropertyValues().size() == 1, "Material exposes accepted common metadata/layout/values");
        float roughness{};
        check(material.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness) && roughness == .31f,
            "Common typed getter reads stable-ID graph override");
        const auto before = material.GetMaterialGraphInstance();
        check(material.TrySetFloat("LXMaterialProperties.lx_bound_p900", .47f) &&
            material.GetMaterialGraphInstance() != before && material.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness) && roughness == .47f,
            "Common typed setter publishes a new graph instance");
        check(material.GetMaterialGraphInstance()->description.parameters[0].id == 900 &&
            std::get<double>(material.GetMaterialGraphInstance()->description.parameters[0].value) == double(.47f),
            "Common editing retains source ID in serialized override");
        std::vector<std::uint8_t> bytes;
        check(material.BuildShaderPropertyBlock(shader.meta, shader.layout, bytes, error) &&
            std::ranges::equal(bytes, material.GetConstantBufferData()), "Common frame property block equals accepted instance bytes");
        const auto accepted = material.GetMaterialGraphInstance();
        check(!material.TrySetFloat("LXMaterialProperties.lx_bound_p900", std::numeric_limits<float>::quiet_NaN()) &&
            material.GetMaterialGraphInstance() == accepted, "Invalid common edit preserves accepted generation");
        auto foreign = shader.meta;
        foreign.generatedMaterial->generation[0] = foreign.generatedMaterial->generation[0] == '0' ? '1' : '0';
        const auto retainedBytes = bytes;
        check(!material.BuildShaderPropertyBlock(foreign, shader.layout, bytes, error) && bytes == retainedBytes,
            "Property block rejects a different generated schema without replacing bytes");
        Material clone(material);
        check(clone.TrySetFloat("LXMaterialProperties.lx_bound_p900", .82f) && material.GetMaterialGraphInstance() == accepted &&
            clone.GetMaterialGraphInstance()->generation == generation, "Clone editing preserves original values and shared shader owner");
        data->FinalizeMaterialRuntime(clone);
        check(clone.GetGeneratedShaderMeta() == &shader.meta, "Scene clone finalization preserves generated common schema");
        Authoring::WriteDocument saved;
        check(data->SerializeMaterialPayload(material, saved.Root()), "Serialize stable-ID instance document");
        const auto node = saved.Root().Read();
        check(node.IsMap(), "Saved instance has a structured root");
        Material restored;
        check(data->DeserializeMaterialPayload(restored, Authoring::NodeViewAccess::Make(node)) &&
            restored.GetGeneratedShaderMeta() == &shader.meta && std::ranges::equal(restored.GetConstantBufferData(), bytes),
            "Reopen rebuilds common material from cooked generation and stable-ID overrides");
        std::stringstream binary(std::ios::in | std::ios::out | std::ios::binary);
        check(data->SerializeMaterialBinaryPayload(material, binary), "Cook instance document");
        Material decoded;
        check(data->DeserializeMaterialBinaryPayload(decoded, binary) &&
            std::ranges::equal(decoded.GetConstantBufferData(), bytes), "Binary instance restores common packed values");
        RunCookedGraphicsIdentityTests(generation->cooked.product);
        check(!GetModuleHandleW(L"slang-compiler.dll"), "Packaged material consumption never loads the Slang compiler");
        check(Authoring::GetTextParseTelemetry().calls == parserCallsBeforePackage,
            "Packaged common contract and instance restore never call an authoring text parser");

        // After the source-free Player boundary, explicitly switch to authoring
        // and prove the warm cache restores the same common contract without
        // recreating any generated source file.
        const auto authoring = LX::LXMaterialAsset::Load(std::filesystem::absolute(argv[3]), LX::CreateMaterialDefinitions(), &error);
        check(!!authoring, "Open authoring fixture for warm cache gate");
        paths->ShaderSourcePath = std::filesystem::absolute(argv[2]) / "Dynamic_CPP/Assets/Shaders";
        paths->BaseProjectPath = std::filesystem::absolute(argv[2]) / "Dynamic_CPP";
        paths->CacheRoot = root.parent_path() / "AuthoringCache";
        paths->AssetAuthoringEnabled = true;
        Material authored;
        const bool coldPrepared = data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error);
        check(coldPrepared, "Cold authoring common generation: " + error);
        const auto cold = authored.GetMaterialGraphInstance();
        const auto sourcePath = paths->CacheRoot / "Lattice" / (guid.ToString() + ".slang");
        const auto cachePath = std::filesystem::path(sourcePath.string() + ".scene-cache");
        check(std::filesystem::exists(cachePath) && std::filesystem::exists(sourcePath), "Cold authoring writes source and LXMC v4 cache");
        const auto cacheTime = std::filesystem::last_write_time(cachePath);
        std::filesystem::rename(sourcePath, sourcePath.string() + ".offline");
        const auto generatedRoot = sourcePath.parent_path() / (sourcePath.stem().string() + ".generated");
        std::filesystem::rename(generatedRoot, generatedRoot.string() + ".offline");
        check(data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error) &&
            authored.GetMaterialGraphInstance()->generation == cold->generation && !std::filesystem::exists(sourcePath) &&
            !std::filesystem::exists(generatedRoot) && std::filesystem::last_write_time(cachePath) == cacheTime,
            "Warm authoring cache restores common generation without recompiling or source pair access");
        check(authored.GetGeneratedShaderMeta() && authored.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness) && roughness == .31f,
            "Warm generation preserves stable override through common Material API");
        RunMaterialCodeRuntimeTests(paths->CacheRoot / "CodeRuntime");
        RunMaterialPipelineRuntimeTests(std::filesystem::absolute(argv[2]));
        data->Finalize();
        check(material.GetGeneratedShaderMeta() == &shader.meta && material.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness),
            "Material retains immutable generated owner after DataSystem finalization");
        std::cout << "MAT7_COMMON_CONSUMER_OK checks=" << checks << " sourceFree=true packagedCompilerFree=true commonEditing=true warmCache=true\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "MAT7_COMMON_CONSUMER_FAIL " << exception.what() << '\n';
        return 1;
    }
}
