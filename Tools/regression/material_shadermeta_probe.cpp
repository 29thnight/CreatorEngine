#include "material_owner_checks.h"
#include "../../Engine/RenderEngine/MaterialGraphShaderMeta.h"
#include "../../Engine/RenderEngine/MaterialGraphSceneCompiler.h"
#include "../../Engine/RenderEngine/MaterialGraphRuntime.h"
#include "../../Engine/RenderEngine/MaterialPropertyPacker.h"
#include "../../Engine/Utility_Framework/AuthoringCookedDocument.h"
#include "../../Engine/Utility_Framework/AuthoringParsedDocument.h"
#include "../../Engine/Utility_Framework/AuthoringParseTelemetry.h"
#include "../../Engine/Utility_Framework/PathFinder.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using namespace LX;
using namespace material_graph;
std::size_t checks{};
void Check(bool valid, const std::string& reason)
{
    ++checks;
    if (!valid) throw std::runtime_error(reason);
}
std::string Read(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    Check(static_cast<bool>(file), "Cannot read fixture");
    return {std::istreambuf_iterator<char>(file), {}};
}
void Write(const std::filesystem::path& path, std::string_view text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    Check(static_cast<bool>(file), "Cannot write fixture");
}
Id PinId(const LXGraph& graph, Id node, std::string_view identifier, Direction direction)
{
    for (const auto& pin : graph.FindNode(node)->pins)
        if (pin.Identifier() == identifier && pin.direction == direction) return pin.id;
    throw std::runtime_error("Missing fixture pin");
}

void Run(const std::filesystem::path& repo, const std::filesystem::path& work)
{
    std::filesystem::create_directories(work);
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = repo / "Dynamic_CPP";
    paths->ShaderSourcePath = repo / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const auto shaderRoot = paths->ShaderSourcePath / "DefaultPassShader";
    std::string error;
    const FileGuid guid{"11111111-1111-4111-8111-111111111111"};
    const std::string texture = "22222222-2222-4222-8222-222222222222";

    LXMaterialProgram program;
    program.surface = true;
    program.features = 1;
    program.semanticKey = "adapter-typed-fixture";
    program.parameters = {
        {11, "enabled", "Enabled", PinType::Bool, true},
        {12, "count", "Count", PinType::Int, std::int64_t{-17}},
        {13, "amount", "Amount", PinType::Float, 0.23},
        {14, "vector", "Vector", PinType::Vector, std::array<double, 3>{0.2, -0.3, 0.4}},
        {15, "normal", "Normal", PinType::Normal, std::array<double, 3>{0, 0, 1}},
        {16, "tint", "Tint \"quoted\"\n색", PinType::Color, std::array<double, 4>{0.8, 0.2, 0.1, 1}, LXColorSpace::SRGB, false},
        {17, "image", "Image", PinType::Texture, texture, LXColorSpace::SRGB},
        {18, "sampler", "Sampler", PinType::Sampler, std::string{"linear-repeat"}}
    };
    program.resources = {
        {LXMaterialResourceKind::Texture, 0, 17, texture, LXColorSpace::SRGB},
        {LXMaterialResourceKind::Texture, 1, 17, texture, LXColorSpace::Data},
        {LXMaterialResourceKind::Sampler, 0, 18, "linear-repeat"}
    };
    // Independent Slang declarations exercise all supported physical types.
    program.slang = R"(
struct LXMaterialParameters { bool lx_p11; int lx_p12; float lx_p13; float3 lx_p14; float3 lx_p15; float4 lx_p16; };
LXMaterialParameters LXDefaultMaterialParameters() { return (LXMaterialParameters)0; }
Texture2D<float4> lx_texture_0 : register(t0, space1);
Texture2D<float4> lx_texture_1 : register(t1, space1);
SamplerState lx_sampler_0 : register(s0, space1);
)";
    const std::string source = BuildBoundSource(program) + R"(
RWStructuredBuffer<float4> result : register(u0);
[shader("compute")] [numthreads(1,1,1)] void CSMain() {
    LXMaterialParameters p = LXBoundMaterialParameters();
    result[0] = float4(p.lx_p14 + p.lx_p15, float(p.lx_p11) + p.lx_p12 + p.lx_p13) + p.lx_p16
        + lx_texture_0.SampleLevel(lx_sampler_0,float2(0.5),0)
        + lx_texture_1.SampleLevel(lx_sampler_0,float2(0.5),0);
}
)";
    const auto sourcePath = work / "fixture.slang";
    Write(sourcePath, source);
    std::vector<RHIShaderReflection> reflections;
    for (auto backend : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        RHIShaderCompiler::VerifiedShader shader;
        Check(RHIShaderCompiler::VerifyFile(sourcePath.string(), "CSMain", "cs_6_0", backend, {}, shader, error),
            "Actual Slang compile/reflection: " + error);
        reflections.push_back(std::move(shader.reflection));
    }
    ShaderPassDesc pass;
    pass.name = "Fixture";
    pass.compute = ShaderStageEntry{"CSMain"};
    pass.queue = ShaderPassQueue::Compute;
    const std::vector<ShaderPassDesc> passes{pass};
    GeneratedMaterialShader accepted;
    const auto publishedRoot = work / "published";
    Check(PublishMaterialShaderMeta(program, guid, source, passes, reflections, publishedRoot, accepted, error),
        "Publish common ShaderMeta: " + error);
    const auto acceptedPath = accepted.metaPath;
    const auto acceptedText = Read(acceptedPath);
    const auto acceptedSource = Read(accepted.meta.ResolveSource(acceptedPath));
    Check(accepted.meta.properties.size() == 8 && accepted.layout.properties.size() == 8 &&
        accepted.layout.samplers.size() == 1, "All typed properties, texture aliases and sampler preserved");
    Check(accepted.meta.properties[5].label == program.parameters[5].name &&
        accepted.meta.properties[5].semantic == "color" && accepted.meta.properties[5].colorSpace == "srgb" &&
        !accepted.meta.properties[5].exposed && accepted.meta.properties[4].semantic == "normal" &&
        accepted.meta.properties[6].parameterId == 17 && accepted.meta.properties[7].parameterId == 17 &&
        accepted.meta.generatedMaterial->samplers[0].parameterId == 18, "Authoring meaning/source identity retained");
    Check(PublishMaterialShaderMeta(program, guid, source, passes, reflections, publishedRoot, accepted, error) &&
        accepted.metaPath == acceptedPath && Read(acceptedPath) == acceptedText, "Deterministic pair reused");

    BindingLayout legacy;
    std::vector<LXMaterialDiagnostic> diagnostics;
    Check(ResolveBindings(program, reflections[0], {}, legacy, diagnostics), "Prior product reflected layout");
    std::vector<std::uint8_t> expected;
    Check(PrepareUniforms(legacy, {}, expected, diagnostics), "Prior default uniform packing");
    std::vector<std::uint8_t> packed(accepted.layout.constantBufferByteSize);
    for (const auto& property : accepted.meta.properties)
    {
        MaterialPropertyValue value;
        const auto* binding = MaterialPropertyPacker::FindBinding(accepted.layout, property.name);
        Check(binding && MaterialPropertyPacker::ApplyDefault(property, value, error) &&
            MaterialPropertyPacker::PackProperty(property, *binding, value, packed, error), "Common default pack: " + error);
    }
    Check(packed == expected, "Common and previous numeric packing match bit for bit");
    Generation generationValue;
    generationValue.assetId.value = guid.m_guid;
    generationValue.generation = 1;
    generationValue.cooked.product.program = program;
    generationValue.cooked.product.layout = legacy;
    generationValue.cooked.product.materialShader = std::make_shared<GeneratedMaterialShader>(accepted);
    const auto owning = own::make_shared<const Generation>(std::move(generationValue));
    const auto lifetime = std::make_shared<int>(1);
    const TextureLoader loader = [&lifetime](const experiment::AssetId&, LXColorSpace, std::string&) {
        return std::shared_ptr<Texture>(lifetime, reinterpret_cast<Texture*>(lifetime.get()));
    };
    InstanceDescription instanceDescription{owning->assetId, {}, {}};
    own::shared_owner<const Instance> instance;
    Check(BuildInstance(owning, instanceDescription, loader, instance, error) && instance->uniforms == expected &&
        instance->properties.size() == 8 && instance->textureOwners.size() == 2,
        "Runtime instance uses common property packing and texture ownership");
    instanceDescription.parameters = {{11, false}, {12, std::int64_t{37}}, {13, .81},
        {14, std::array<double, 3>{.9, .8, .7}}, {15, std::array<double, 3>{1, 0, 0}}};
    Check(PrepareUniforms(legacy, instanceDescription.parameters, expected, diagnostics) &&
        BuildInstance(owning, instanceDescription, loader, instance, error) && instance->uniforms == expected,
        "Common Bool/Int/Float/Vector/Normal override packing is bit exact");
    const experiment::AssetId replacement{FileGuid{"44444444-4444-4444-8444-444444444444"}.m_guid};
    instanceDescription.textures = {{17, replacement}};
    Check(BuildInstance(owning, instanceDescription, loader, instance, error) &&
        instance->properties[6].m_textureGuid.m_guid == replacement.value &&
        instance->properties[7].m_textureGuid.m_guid == replacement.value &&
        instance->textures[0].assetId == replacement && instance->textures[1].assetId == replacement,
        "Stable texture override changes every SRGB/data resource alias");
    const auto retainedInstance = instance;
    instanceDescription.parameters.push_back({16, std::array<double, 4>{1, 0, 0, 1}});
    Check(!BuildInstance(owning, instanceDescription, loader, instance, error) && material_graph_test::SamePinnedObject(instance, retainedInstance),
        "Private common property edit preserves accepted snapshot");
    instanceDescription.parameters.back() = {13, true};
    Check(!BuildInstance(owning, instanceDescription, loader, instance, error) && material_graph_test::SamePinnedObject(instance, retainedInstance),
        "Duplicate/type-invalid common edit preserves accepted snapshot");

    const auto document = Authoring::ParsedDocument::ParseText(acceptedText, error);
    std::vector<std::byte> cooked;
    Check(document && Authoring::EncodeCookedDocument(document.Root(), cooked, error), "Common metadata cooked encoding");
    const auto decoded = Authoring::DecodeCookedDocument(cooked, error);
    ShaderMeta roundtrip;
    Check(decoded && ShaderMetaLoader::ParseDocument(decoded->Root(), acceptedPath, guid, roundtrip, error) &&
        roundtrip.generatedMaterial->generation == accepted.meta.generatedMaterial->generation &&
        roundtrip.properties[5].label == program.parameters[5].name, "Cooked document retains generated contract");
    Check(!ShaderMetaLoader::Parse("{\"unknownField\":true," + acceptedText.substr(1), acceptedPath, guid,
        roundtrip, error) && error.find("unknownField") != std::string::npos, "Unknown schema field rejected");
    auto wrongSemantic = acceptedText;
    const auto semanticOffset = wrongSemantic.find("\"semantic\":\"color\"");
    Check(semanticOffset != std::string::npos, "Color semantic fixture present");
    wrongSemantic.replace(semanticOffset, std::string_view{"\"semantic\":\"color\""}.size(), "\"semantic\":\"normal\"");
    Check(!ShaderMetaLoader::Parse(wrongSemantic, acceptedPath, guid, roundtrip, error) &&
        error.find("semantic/type mismatch") != std::string::npos, "Semantic/type mismatch rejected");

    auto badReflections = reflections;
    for (auto& resource : badReflections[1].resources)
        if (resource.name == "LXMaterialProperties") resource.fields[0].type.scalar = RHIShaderScalarKind::Float32;
    Check(!PublishMaterialShaderMeta(program, guid, source + "\n// changed\n", passes, badReflections,
        publishedRoot, accepted, error) && accepted.metaPath == acceptedPath, "Backend disagreement rejected without replacing accepted pair");
    auto badRange = reflections;
    for (auto& reflection : badRange)
        for (auto& resource : reflection.resources)
            if (resource.name == "LXMaterialProperties") resource.fields[0].byteOffset = resource.byteSize;
    Check(!PublishMaterialShaderMeta(program, guid, source + "\n// bad offset\n", passes, badRange,
        publishedRoot, accepted, error) && error.find("exceeds its reflected buffer") != std::string::npos,
        "Out-of-buffer reflected offset rejected before publication");
    auto missingSampler = reflections;
    for (auto& reflection : missingSampler)
        std::erase_if(reflection.resources, [](const auto& resource) { return resource.name == "lx_sampler_0"; });
    Check(!PublishMaterialShaderMeta(program, guid, source + "\n// missing sampler\n", passes, missingSampler,
        publishedRoot, accepted, error) && error.find("sampler is missing") != std::string::npos,
        "Missing reflected sampler rejected");
    auto overflow = program;
    overflow.parameters[1].value = std::numeric_limits<std::int64_t>::max();
    Check(!PublishMaterialShaderMeta(overflow, guid, source, passes, reflections, publishedRoot, accepted, error) &&
        accepted.metaPath == acceptedPath, "Integer overflow rejected");
    overflow = program;
    overflow.parameters[2].value = std::numeric_limits<double>::max();
    Check(!PublishMaterialShaderMeta(overflow, guid, source, passes, reflections, publishedRoot, accepted, error), "Float overflow rejected");
    auto invalid = program;
    invalid.resources[0].reference = "not-an-asset-guid";
    Check(!PublishMaterialShaderMeta(invalid, guid, source, passes, reflections, publishedRoot, accepted, error), "Invalid texture asset identity rejected");
    Check(Read(acceptedPath) == acceptedText && Read(accepted.meta.ResolveSource(acceptedPath)) == acceptedSource,
        "Failure preserved accepted file bytes");
    std::size_t directories = 0;
    for (const auto& entry : std::filesystem::directory_iterator(publishedRoot))
    {
        Check(!entry.path().filename().string().starts_with(".candidate-"), "Failed candidate cleaned");
        if (entry.is_directory()) ++directories;
    }
    Check(directories == 1, "No failed candidate generation published");

    const auto mixed = work / "mixed";
    std::filesystem::create_directories(mixed);
    Write(mixed / "material.shadermeta", acceptedText);
    Write(mixed / "material.slang", source + "\n// stale pair\n");
    const auto priorGeneration = roundtrip.generatedMaterial->generation;
    Check(!ShaderMetaLoader::LoadFile(mixed / "material.shadermeta", guid, roundtrip, error) &&
        error.find("SHA-256 mismatch") != std::string::npos && roundtrip.generatedMaterial->generation == priorGeneration,
        "Mismatched source rejected while preserving prior parsed metadata");

    // Exercise the same adapter through the production Scene compiler using a
    // newly authored Graph, rather than only constructing metadata fixtures.
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    asset.activeOutput = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    const auto parameter = asset.CreateNode("LXParameterFloat", -200, 0);
    asset.blackboard.push_back({900, "roughness", "Roughness", PinType::Float, 0.37});
    Check(asset.graph.SetProperty(parameter, "parameter", "900"), "New Graph Blackboard identity");
    Check(asset.graph.Connect(PinId(asset.graph, parameter, "Value", Direction::Output),
        PinId(asset.graph, surface, "Roughness", Direction::Input)).has_value(), "New Graph parameter connection");
    Check(asset.graph.Connect(PinId(asset.graph, surface, "BSDF", Direction::Output),
        PinId(asset.graph, asset.activeOutput, "Surface", Direction::Input)).has_value(), "New Graph output connection");
    const auto generated = GenerateMaterialSlang(asset, &diagnostics);
    Check(generated.has_value(), "New Graph Slang generation");
    const auto assets = work / "Assets";
    std::filesystem::create_directories(assets);
    Check(asset.Save(assets / "fixture.shadergraph", &error), "New Graph source saved for actual AssetCooker");
    Write(assets / "fixture.shadergraph.meta", "guid: " + guid.ToString() + "\n");
    VerifiedProduct product;
    Check(CompileSceneProduct(*generated, shaderRoot, work / "scene.slang", {}, product, error, guid),
        "Production Scene compiler generated pair: " + error);
    std::size_t scenePairs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(work / "scene.generated"))
    {
        ShaderMeta meta;
        Check(ShaderMetaLoader::LoadFile(entry.path() / "material.shadermeta", guid, meta, error) &&
            meta.generatedMaterial && meta.properties.size() == 1 && meta.properties[0].parameterId == 900 &&
            meta.passes.size() == 6, "Actual Scene generation has common metadata/complete declared stage pairs including Forward");
        ++scenePairs;
    }
    Check(scenePairs == 1, "One verified production pair");
    Check(product.materialShader && product.materialShader->meta.guid == guid,
        "Production product retains the generated common shader owner");
    std::vector<std::uint8_t> payload;
    const auto parserCallsBeforeRestore = Authoring::GetTextParseTelemetry().calls;
    const bool cookedGenerated = WriteCookedProgram(product, {}, payload, error);
    Check(cookedGenerated, "Cook complete generated shader generation: " + error);
    CookedProgram restoredProduct;
    Check(ReadCookedProgram(payload, {}, restoredProduct, error) && restoredProduct.product.materialShader &&
        restoredProduct.product.materialShader->layout == product.materialShader->layout &&
        restoredProduct.product.materialShader->document == product.materialShader->document &&
        restoredProduct.product.materialShader->source == product.materialShader->source,
        "Cooked common schema/layout/source round trip: " + error);
    std::filesystem::rename(work / "scene.generated", work / "scene.generated.offline");
    std::filesystem::remove(work / "scene.slang");
    Check(ReadCookedProgram(payload, {}, restoredProduct, error) &&
        !std::filesystem::is_regular_file(restoredProduct.product.materialShader->meta.ResolveSource(
            restoredProduct.product.materialShader->meta.originPath)),
        "Cooked generated contract restores with no source pair on disk");
    Check(Authoring::GetTextParseTelemetry().calls == parserCallsBeforeRestore,
        "Cooked generated contract validates, recooks and restores without authoring text parsing");
    const auto retained = restoredProduct.product.materialShader;
    auto alteredContract = Authoring::WriteDocument::ParseText(product.materialShader->document, &error);
    Check(alteredContract.has_value(), "Prepare valid altered cooked contract");
    alteredContract->Root().Child("properties").At(0).Child("default").SetScalar(0.91f);
    std::vector<std::byte> alteredContractBytes;
    Check(Authoring::EncodeCookedDocument(alteredContract->Root().Read(), alteredContractBytes, error),
        "Encode altered cooked contract");
    GeneratedMaterialShader retainedShader = *retained;
    Check(!RestoreMaterialShaderMeta(product, guid, product.materialShader->document, product.materialShader->source,
        retainedShader, error, alteredContractBytes) && retainedShader.meta == retained->meta,
        "Cooked typed defaults cannot disagree with verified program or replace accepted output");
    auto broken = product;
    auto brokenShader = std::make_shared<GeneratedMaterialShader>(*broken.materialShader);
    brokenShader->source += "\n// mismatched source\n";
    broken.materialShader = brokenShader;
    const auto acceptedPayload = payload;
    Check(!WriteCookedProgram(broken, {}, payload, error) && payload == acceptedPayload,
        "Mismatched generated pair cannot replace accepted cook bytes");
    brokenShader->source = product.materialShader->source;
    brokenShader->layout.properties[0].byteOffset += 4;
    Check(!WriteCookedProgram(broken, {}, payload, error) && payload == acceptedPayload,
        "Altered common binding cannot replace accepted cook bytes");
    auto corrupt = acceptedPayload;
    corrupt[corrupt.size() / 2] ^= 1;
    Check(!ReadCookedProgram(corrupt, {}, restoredProduct, error) && restoredProduct.product.materialShader == retained,
        "Corrupt cooked generation preserves prior schema owner");

    // The editor compiles only its renderer's backend. That product must survive the
    // editor cache format and load for its backend, and must refuse the other one.
    VerifiedProduct dxilOnly;
    Check(CompileSceneProduct(*generated, shaderRoot, work / "scene-dxil.slang", {}, dxilOnly, error, guid,
        RHIShaderBinary::Dxil), "Editor compiles only the running backend: " + error);
    Check(std::ranges::all_of(dxilOnly.targets, [](const auto& target) { return target.binary == RHIShaderBinary::Dxil; }) &&
        dxilOnly.targets.size() * 2 == product.targets.size(),
        "One-backend product carries exactly that backend's complete stage set");
    std::vector<std::uint8_t> dxilPayload;
    CookedProgram dxilRestored;
    Check(WriteCookedProgram(dxilOnly, {}, dxilPayload, error) && ReadCookedProgram(dxilPayload, {}, dxilRestored, error),
        "One-backend product round-trips the editor cache format: " + error);
    SceneShaderSet dxilShaders;
    Check(LoadSceneShaders(dxilRestored.product, RHIShaderBinary::Dxil, dxilShaders, error) &&
        dxilShaders.color.bytecode.Size() > 0, "One-backend product loads for its backend: " + error);
    Check(!LoadSceneShaders(dxilRestored.product, RHIShaderBinary::SpirV, dxilShaders, error),
        "One-backend product refuses the backend it does not carry");

    std::size_t legacyFiles = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(shaderRoot))
        if (entry.path().extension() == ".shadermeta")
        {
            ShaderMeta meta;
            Check(ShaderMetaLoader::LoadFile(entry.path(), guid, meta, error) && !meta.generatedMaterial,
                "Existing authored ShaderMeta remains valid: " + error);
            ++legacyFiles;
        }
    Check(legacyFiles == 6, "Existing authored corpus tested");
    std::cout << "MAT7_SHADERMETA_ADAPTER_OK checks=" << checks
        << " backends=2 typedProperties=8 samplers=1 legacyFiles=" << legacyFiles
        << " commonPacking=bitExact deterministic=true failurePreserved=true sceneCompiler=true cookedGenerated=true sourceFree=true\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("Expected repository and unique work directory");
        Run(std::filesystem::absolute(argv[1]), std::filesystem::absolute(argv[2]));
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "MAT7_SHADERMETA_ADAPTER_FAIL " << exception.what() << '\n';
        return 1;
    }
}
