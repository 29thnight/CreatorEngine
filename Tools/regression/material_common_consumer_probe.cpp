#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "DataSystem.h"
#include "Material.h"
#include "MaterialGraphShaderMeta.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringNodeViewAccess.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringParseTelemetry.h"
#include "../../Engine/Utility_Framework/Authoring/AuthoringWriteNode.h"
#include "RHI/RHIShaderCompiler.h"

#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

void RunMaterialCodeRuntimeTests(const std::filesystem::path& root);
void RunCookedGraphicsIdentityTests(const material_graph::VerifiedProduct& product);
void RunMaterialPipelineRuntimeTests(const std::filesystem::path& repository);

namespace
{
    template<class T>
    bool SameGraphOwner(const own::shared_owner<T>& left, const own::shared_owner<T>& right)
    {
        if (left && right)
        {
            return std::addressof(*left) == std::addressof(*right);
        }
        return !left && !right;
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--pipeline-runtime")
        {
            const auto repository = std::filesystem::absolute(argv[2]);
            auto* paths = (&ce::paths::get_state());
            paths->shader_source_path = repository / "Dynamic_CPP/Assets/Shaders";
            paths->base_project_path = repository / "Dynamic_CPP";
            paths->asset_authoring_enabled = true;
            RunMaterialPipelineRuntimeTests(repository);
            return 0;
        }
        if (argc != 4) throw std::runtime_error("Expected source-free packaged Assets root, repository and authoring fixture");
        const auto root = std::filesystem::absolute(argv[1]);
        auto* paths = (&ce::paths::get_state());
        paths->data_path = root;
        paths->base_project_path = root.parent_path();
        paths->asset_authoring_enabled = false;
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
            !SameGraphOwner(material.GetMaterialGraphInstance(), before) && material.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness) && roughness == .47f,
            "Common typed setter publishes a new graph instance");
        check(material.GetMaterialGraphInstance()->description.parameters[0].id == 900 &&
            std::get<double>(material.GetMaterialGraphInstance()->description.parameters[0].value) == double(.47f),
            "Common editing retains source ID in serialized override");
        std::vector<std::uint8_t> bytes;
        check(material.BuildShaderPropertyBlock(shader.meta, shader.layout, bytes, error) &&
            std::ranges::equal(bytes, material.GetConstantBufferData()), "Common frame property block equals accepted instance bytes");
        const auto accepted = material.GetMaterialGraphInstance();
        check(!material.TrySetFloat("LXMaterialProperties.lx_bound_p900", std::numeric_limits<float>::quiet_NaN()) &&
            SameGraphOwner(material.GetMaterialGraphInstance(), accepted), "Invalid common edit preserves accepted generation");
        auto foreign = shader.meta;
        foreign.generatedMaterial->generation[0] = foreign.generatedMaterial->generation[0] == '0' ? '1' : '0';
        const auto retainedBytes = bytes;
        check(!material.BuildShaderPropertyBlock(foreign, shader.layout, bytes, error) && bytes == retainedBytes,
            "Property block rejects a different generated schema without replacing bytes");
        Material clone(material);
        check(clone.TrySetFloat("LXMaterialProperties.lx_bound_p900", .82f) && SameGraphOwner(material.GetMaterialGraphInstance(), accepted) &&
            SameGraphOwner(clone.GetMaterialGraphInstance()->generation, generation), "Clone editing preserves original values and shared shader owner");
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
        paths->shader_source_path = std::filesystem::absolute(argv[2]) / "Dynamic_CPP/Assets/Shaders";
        paths->base_project_path = std::filesystem::absolute(argv[2]) / "Dynamic_CPP";
        paths->cache_root = root.parent_path() / "AuthoringCache";
        paths->asset_authoring_enabled = true;
        Material authored;
        const bool coldPrepared = data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error);
        check(coldPrepared, "Cold authoring common generation: " + error);
        const auto cold = authored.GetMaterialGraphInstance();
        // Each authoring compile writes its generated source to its own scratch
        // directory and removes it (DataSystem::CompileMaterialGraphAuthoring); the
        // product owns its bytecode. The verified-product cache stays beside the
        // scratch roots, so "warm" means the same generation without a stage compile.
        const auto scratchRoot = paths->cache_root / "Lattice";
        check(std::filesystem::exists(scratchRoot / (guid.ToString() + ".slang.scene-cache")),
            "Cold authoring keeps its verified-product cache outside the scratch directory");
        const auto scratchLeft = [&] {
            std::error_code ignored;
            if (!std::filesystem::exists(scratchRoot, ignored)) return false;
            return std::ranges::any_of(std::filesystem::directory_iterator(scratchRoot),
                [](const auto& entry) { return entry.path().filename().string().starts_with("authoring-"); });
        };
        check(!scratchLeft(), "Cold authoring removes its scratch source pair");
        const auto compiles = [] { return RHIShaderCompiler::GetStats().compiles; };
        const auto sameProduct = [&](const auto& instance) {
            return instance->generation->cooked.product.program.semanticKey ==
                cold->generation->cooked.product.program.semanticKey;
        };
        auto compilesBefore = compiles();
        const bool warmPrepared = data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error);
        check(warmPrepared && sameProduct(authored.GetMaterialGraphInstance()) && compiles() == compilesBefore &&
            !scratchLeft(),
            "Warm authoring restores the same common generation without compiling a stage: prepared=" +
            std::to_string(warmPrepared) + " compiles=" + std::to_string(compiles() - compilesBefore) + " " + error);
        check(authored.GetGeneratedShaderMeta() && authored.TryGetFloat("LXMaterialProperties.lx_bound_p900", roughness) && roughness == .31f,
            "Warm generation preserves stable override through common Material API");

        // 캐시 키는 Slang 이 실제로 읽은 파일만 본다. 셰이더 사본에서 셋을 단정한다:
        // 옮겨도 맞히고, 재질이 안 읽는 셰이더를 고쳐도 맞히며, 기록된 의존 파일을 고치면
        // 다시 컴파일한다. 옛 키(셰이더 폴더 전체)는 둘째에서 다시 컴파일했다.
        const auto shaderCopy = root.parent_path() / "ShaderDependencyCopy";
        std::filesystem::remove_all(shaderCopy);
        std::filesystem::copy(std::filesystem::absolute(argv[2]) / "Dynamic_CPP/Assets/Shaders", shaderCopy,
            std::filesystem::copy_options::recursive);
        paths->shader_source_path = shaderCopy;
        const auto appendComment = [&](const std::filesystem::path& file) {
            check(std::filesystem::exists(file), "Shader to edit exists: " + file.string());
            std::ofstream stream(file, std::ios::binary | std::ios::app);
            stream << "\n// material dependency cache probe\n";
            check(static_cast<bool>(stream), "Edit shader copy: " + file.string());
        };
        compilesBefore = compiles();
        check(data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error) &&
            compiles() == compilesBefore,
            "Relocated shader tree keeps the warm cache: " + error);
        appendComment(shaderCopy / "DefaultPassShader/IblBrdf.slang");
        check(data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error) &&
            compiles() == compilesBefore,
            "Editing a shader the material never reads keeps the warm cache: " + error);
        appendComment(shaderCopy / "DefaultPassShader/Includes/CascadedShadow.slang");
        check(data->ConfigureMaterialGraphAuthoring(authored, *authoring, description, error) &&
            compiles() > compilesBefore,
            "Editing a recorded dependency recompiles the material: " + error);
        paths->shader_source_path = std::filesystem::absolute(argv[2]) / "Dynamic_CPP/Assets/Shaders";
        RunMaterialCodeRuntimeTests(paths->cache_root / "CodeRuntime");
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
