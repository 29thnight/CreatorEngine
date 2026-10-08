#include "support/MaterialPipelineSlot.h"
#include "../../Engine/RenderEngine/MaterialGraphProduct.h"
#include "../../Engine/RenderEngine/RHI/RHIShaderCompiler.h"
#include "../../Engine/RenderEngine/RHI/RHIShaderSource.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedMaterialProgram.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAssetCatalog.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedAudioClipSource.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/PakAudioClipByteSource.h"
#include "../../Engine/Utility_Framework/PathFinder.h"
#include "material_probe_gpu.h"
#include "material_runtime_tests.h"

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace LX;
using namespace material_graph;
std::size_t checks = 0;

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

Id PinId(const LXGraph& graph, Id node, const std::string& name, Direction direction = Direction::Input)
{
    for (const auto& pin : graph.FindNode(node)->pins)
        if (pin.Identifier() == name && pin.direction == direction)
            return pin.id;
    throw std::runtime_error("Missing pin " + name);
}

void Connect(LXGraph& graph, Id from, const std::string& output, Id to, const std::string& input)
{
    Check(graph.Connect(PinId(graph, from, output, Direction::Output), PinId(graph, to, input)).has_value(), "Connect");
}

class PipelineCache final : public IRenderPipelineCache
{
  public:
    PipelineCache()
    {
        MaterialProbe::Require(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device_)),
                               "Product graphics device");
        D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, TextureRegister, 0,
                                     D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[0].Descriptor.ShaderRegister = UniformRegister;
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[1].DescriptorTable = {1, &range};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = SamplerRegister;
        D3D12_ROOT_SIGNATURE_DESC desc{2, params, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        MaterialProbe::ComPtr<ID3DBlob> bytes, errors;
        MaterialProbe::Require(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &bytes, &errors),
                               "Product graphics root serialization");
        MaterialProbe::Require(
            device_->CreateRootSignature(0, bytes->GetBufferPointer(), bytes->GetBufferSize(), IID_PPV_ARGS(&root_)),
            "Product graphics root");
    }
    bool fail = false;
    bool same = false;
    std::uint32_t creations = 0;
    std::vector<std::pair<RHIPipelineHandle, RHICompletionPoint>> retired;
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc, std::string& error) override
    {
        ++creations;
        Check(desc.vsBytecode && desc.psBytecode && desc.vsSize && desc.psSize, "Owned PSO code");
        if (fail)
        {
            error = "Injected PSO failure";
            return {};
        }
        if (same && !pipelines_.empty())
            return {1};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC native{};
        native.pRootSignature = root_.Get();
        native.VS = {desc.vsBytecode, desc.vsSize};
        native.PS = {desc.psBytecode, desc.psSize};
        native.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        native.SampleMask = UINT_MAX;
        native.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        native.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        native.RasterizerState.DepthClipEnable = TRUE;
        native.DepthStencilState.DepthEnable = FALSE;
        native.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        native.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
        native.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
        native.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        native.NumRenderTargets = 1;
        native.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        native.SampleDesc.Count = 1;
        MaterialProbe::ComPtr<ID3D12PipelineState> pipeline;
        const auto status = device_->CreateGraphicsPipelineState(&native, IID_PPV_ARGS(&pipeline));
        if (FAILED(status))
        {
            error = "D3D12 graphics PSO rejected compiled material";
            return {};
        }
        pipelines_.push_back(std::move(pipeline));
        return {same ? 1u : creations};
    }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc&, std::string&) override { return {}; }
    bool InvalidatePipeline(RHIPipelineHandle handle, RHICompletionPoint point) override
    {
        retired.emplace_back(handle, point);
        return true;
    }
    std::uint32_t InvalidatePipelines(RHICompletionPoint) override { return 0; }
    std::uint32_t CollectRetiredPipelines(RHICompletionPoint) override { return 0; }

  private:
    MaterialProbe::ComPtr<ID3D12Device> device_;
    MaterialProbe::ComPtr<ID3D12RootSignature> root_;
    std::vector<MaterialProbe::ComPtr<ID3D12PipelineState>> pipelines_;
};

float Decode(float encoded)
{
    return encoded <= .04045f ? encoded / 12.92f : std::pow((encoded + .055f) / 1.055f, 2.4f);
}

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const auto directory = root / "Build/Obj/MaterialProductProbe";
    std::filesystem::create_directories(directory);
    const auto includeDirectory = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes";
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    const auto image = asset.CreateNode("LXTextureSample", -400, 0);
    const auto imageParameter = asset.CreateNode("LXParameterTexture", -600, 0);
    const auto multiply = asset.CreateNode("LXMultiplyColor", -200, 0);
    const auto roughness = asset.CreateNode("LXParameterFloat", -200, 300);
    const auto tint = asset.CreateNode("LXParameterColor", -400, 300);
    asset.activeOutput = output;
    asset.blackboard = {
        {900, "roughness", "Roughness", PinType::Float, .7},
        {901, "tint", "Tint", PinType::Color, std::array<double, 4>{1, 1, 1, 1}},
        {905, "image", "Image", PinType::Texture, std::string("fixture://encoded"), LXColorSpace::SRGB}};
    Check(asset.graph.SetProperty(roughness, "parameter", "900"), "Roughness property");
    Check(asset.graph.SetProperty(tint, "parameter", "901"), "Tint property");
    Check(asset.graph.SetProperty(imageParameter, "parameter", "905"), "Image parameter");
    Check(asset.graph.SetProperty(image, "colorSpace", "srgb"), "Image storage encoding");
    Connect(asset.graph, imageParameter, "Value", image, "Texture");
    Connect(asset.graph, image, "Color", multiply, "A");
    Connect(asset.graph, image, "Alpha", surface, "Alpha");
    Connect(asset.graph, tint, "Value", multiply, "B");
    Connect(asset.graph, multiply, "Result", surface, "Base Color");
    Connect(asset.graph, roughness, "Value", surface, "Roughness");
    Connect(asset.graph, surface, "BSDF", output, "Surface");
    std::vector<LXMaterialDiagnostic> diagnostics;
    auto generated = GenerateMaterialSlang(asset, &diagnostics, {.dependencies = "product-fixture-v1"});
    Check(generated.has_value(), "Generate product fixture");
    auto program = *generated;
    // Exercise all uniform storage types in the host binding contract, including
    // bool/int types that Principled's numeric sockets do not directly consume.
    program.parameters.push_back({902, "enabled", "Enabled", PinType::Bool, true});
    program.parameters.push_back({903, "count", "Count", PinType::Int, std::int64_t(7)});
    program.parameters.push_back({904, "direction", "Direction", PinType::Vector, std::array<double, 3>{1, 2, 3}});
    const auto members = program.slang.find("\n};", program.slang.find("struct LXMaterialParameters"));
    program.slang.insert(members, "\n    bool lx_p902;\n    int lx_p903;\n    float3 lx_p904;");
    const auto defaults = program.slang.find("    return parameters;");
    program.slang.insert(defaults, "    parameters.lx_p902 = true;\n    parameters.lx_p903 = 7;\n"
                                   "    parameters.lx_p904 = float3(1, 2, 3);\n");
    program.semanticKey += "host-fixture-bool-int-vector-v1";
    const auto sourceFile = directory / "bound.slang";
    const std::string entry = R"(
StructuredBuffer<float4> probeInputs : register(t0);
RWStructuredBuffer<float4> probeOutputs : register(u0);
[shader("compute")]
[numthreads(1, 1, 1)]
void CSMain(uint3 thread : SV_DispatchThreadID)
{
    LXMaterialContext context;
    context.uv = float3(probeInputs[thread.x].xy, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    LXMaterialParameters parameters = LXBoundMaterialParameters();
    MaterialInputs material = LXGenerateMaterial(context, parameters).surfaceInputs;
    probeOutputs[thread.x * 3] = float4(material.baseColor, material.roughness);
    probeOutputs[thread.x * 3 + 1] = float4(parameters.lx_p904, float(parameters.lx_p903));
    probeOutputs[thread.x * 3 + 2] = float4(float(parameters.lx_p902), 0, 0, material.alpha);
}
[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    return float4(id == 0 ? -1 : 1, id == 2 ? -1 : 1, 0, 1);
}
[shader("fragment")]
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    LXMaterialContext context;
    context.uv = float3(position.xy / 64.0, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    LXMaterialParameters parameters = LXBoundMaterialParameters();
    MaterialInputs material = LXGenerateMaterial(context, parameters).surfaceInputs;
    return float4(material.baseColor + parameters.lx_p904 * 0.01,
        material.roughness + float(parameters.lx_p903) * 0.01 + float(parameters.lx_p902) * 0.01);
}
)";
    std::ofstream source(sourceFile, std::ios::binary | std::ios::trunc);
    source << BuildBoundSource(program) << "\n#include \"dependency.slang\"\n" << entry;
    source.close();
    Check(source.good(), "Write bound source");
    std::ofstream dependency(directory / "dependency.slang", std::ios::binary | std::ios::trunc);
    dependency << "// Dependency identity fixture v1\n";
    dependency.close();
    RHIShaderCompileOptions options;
    options.includeDirectories.push_back(includeDirectory);
    RHIShaderPermutation permutation;
    std::string error;
    std::vector<LXMaterialShaderArtifact> shaders;
    RHIShaderReflection reflection;
    RHIShaderBlob vs, ps, cs;
    for (const auto binary : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        RHIShaderCompiler::ScopedOutput target(binary);
        const std::string backend = binary == RHIShaderBinary::Dxil ? "dxil" : "spirv";
        for (const auto& [name, profile] : {std::pair{"VSMain", "vs_6_0"}, {"PSMain", "ps_6_0"}, {"CSMain", "cs_6_0"}})
        {
            RHIShaderBlob code;
            Check(RHIShaderCompiler::CompileFile(sourceFile.string(), name, profile, code, error, options),
                  "Product compiler " + backend + "/" + name + ": " + error);
            const auto* begin = static_cast<const std::uint8_t*>(code.Data());
            shaders.push_back({backend, name, {begin, begin + code.Size()}});
            if (binary == RHIShaderBinary::Dxil)
            {
                if (std::string_view(name) == "VSMain")
                    vs = code;
                else if (std::string_view(name) == "PSMain")
                    ps = code;
                else
                    cs = code;
            }
        }
        RHIShaderReflection reflected;
        Check(RHIShaderCompiler::ReflectFile(sourceFile.string(), "CSMain", "cs_6_0", binary, permutation, reflected,
                                             error, options),
              "Reflect CS: " + error);
        if (binary == RHIShaderBinary::Dxil)
            reflection = reflected;
        else
            Check(AreShaderReflectionsEquivalent(reflection, reflected, error), "Backend binding parity: " + error);
    }
    BindingLayout layout;
    Budget budget;
    Check(ResolveBindings(program, reflection, budget, layout, diagnostics), "Resolve actual product reflection");
    Check(layout.parameters.size() == 5 && layout.textures.size() == 1 && layout.samplers.size() == 1,
          "Reflected logical resources");
    Capabilities capabilities;
    capabilities.coreForward = true;
    VerifiedProduct verified;
    const std::vector<CompileTarget> targets{
        {RHIShaderBinary::Dxil, "VSMain", "vs_6_0"},  {RHIShaderBinary::Dxil, "PSMain", "ps_6_0"},
        {RHIShaderBinary::Dxil, "CSMain", "cs_6_0"},  {RHIShaderBinary::SpirV, "VSMain", "vs_6_0"},
        {RHIShaderBinary::SpirV, "PSMain", "ps_6_0"}, {RHIShaderBinary::SpirV, "CSMain", "cs_6_0"}};
    Check(
        VerifyProduct(program, sourceFile, targets, permutation, options, capabilities, budget, verified, diagnostics),
        "Verified product transaction");
    Check(verified.program.semanticKey != program.semanticKey && verified.shaders.size() == 6 &&
              verified.layout == layout,
          "Compiler/include/host identity included in cooked program");
    const auto verifiedIdentity = verified.program.semanticKey;
    auto reversedTargets = targets;
    std::ranges::reverse(reversedTargets);
    VerifiedProduct reordered;
    Check(VerifyProduct(program, sourceFile, reversedTargets, permutation, options, capabilities, budget, reordered,
                        diagnostics) &&
              reordered.program.semanticKey == verifiedIdentity,
          "Compile target order does not change specialization identity");
    auto incompleteTargets = targets;
    incompleteTargets.pop_back();
    incompleteTargets.pop_back();
    Check(!VerifyProduct(program, sourceFile, incompleteTargets, permutation, options, capabilities, budget, verified,
                         diagnostics) &&
              verified.program.semanticKey == verifiedIdentity,
          "Partial backend target set preserves verified product");
    auto wrongTargets = targets;
    wrongTargets[0].entry = "MissingVertex";
    Check(!VerifyProduct(program, sourceFile, wrongTargets, permutation, options, capabilities, budget, verified,
                         diagnostics) &&
              verified.program.semanticKey == verifiedIdentity,
          "Actual compile failure preserves all verified targets");
    RHIShaderCompiler::VerifiedShader firstDependency, nextDependency;
    Check(RHIShaderCompiler::VerifyFile(sourceFile.string(), "CSMain", "cs_6_0", RHIShaderBinary::Dxil, permutation,
                                        firstDependency, error, options),
          "Verify dependency fixture");
    dependency.open(directory / "dependency.slang", std::ios::binary | std::ios::trunc);
    dependency << "// Dependency identity fixture v2\n";
    dependency.close();
    Check(RHIShaderCompiler::VerifyFile(sourceFile.string(), "CSMain", "cs_6_0", RHIShaderBinary::Dxil, permutation,
                                        nextDependency, error, options) &&
              firstDependency.dependencyIdentity != nextDependency.dependencyIdentity,
          "Actual included-file change invalidates compiler identity");
    dependency.open(directory / "dependency.slang", std::ios::binary | std::ios::trunc);
    dependency << "// Dependency identity fixture v1\n";
    dependency.close();
    Selection selected;
    Check(SelectRoute(program, capabilities, budget, selected, diagnostics) && selected.route == Route::Forward &&
              selected.tier == Tier::Standard,
          "Core IOR routed without GBuffer truncation");
    auto deferred = program;
    deferred.features = 0x003F;
    Check(SelectRoute(deferred, {}, budget, selected, diagnostics) && selected.route == Route::Deferred,
          "Representable Standard selects Deferred");
    auto layered = program;
    layered.features |= 0x0080;
    Check(!SelectRoute(layered, capabilities, budget, selected, diagnostics), "Missing layered lookup rejected");
    capabilities.layeredLookup = true;
    Check(SelectRoute(layered, capabilities, budget, selected, diagnostics) && selected.tier == Tier::Layered,
          "Layered Forward selected");
    for (const auto flag : {0x0800u, 0x1000u, 0x2000u})
    {
        auto special = program;
        special.features |= flag;
        special.volume = flag == 0x2000;
        Check(!SelectRoute(special, capabilities, budget, selected, diagnostics), "Missing special transport rejected");
    }
    auto invalid = program;
    invalid.features |= 0x8000;
    Check(!SelectRoute(invalid, capabilities, budget, selected, diagnostics), "Unknown feature rejected");
    auto tight = budget;
    tight.textureSamples = 0;
    Check(!SelectRoute(program, capabilities, tight, selected, diagnostics), "Sample budget enforced");
    auto owner = std::make_shared<const int>(27);
    const TextureBinding texture{0, {{1}, RHIFormat::RGBA8UnormSrgb, 2, 2, 1, 1, false}, owner};
    const std::vector<ParameterOverride> overrides{{900, .25},
                                                   {901, std::array<double, 4>{.5, .25, 2, .75}},
                                                   {902, false},
                                                   {903, std::int64_t(-9)},
                                                   {904, std::array<double, 3>{4, 5, 6}}};
    ResourcePacket packet;
    Check(PrepareResources(layout, overrides, std::span(&texture, 1), packet, diagnostics), "Prepare resources");
    Check(packet.owners.size() == 1 && packet.owners[0] == owner, "Own texture generation");
    MaterialProbe::TextureFixture tex;
    tex.width = tex.height = 2;
    tex.srgb8 = true;
    tex.pixels = {{0, 0, 0, 0}, {1, 1, 1, 1}, {.2f, .4f, .6f, .8f}, {.8f, .6f, .4f, .2f}};
    MaterialProbe::BindingFixture bindings;
    bindings.textureRegister = TextureRegister;
    bindings.samplerRegister = SamplerRegister;
    bindings.space = 0;
    bindings.uniforms = packet.uniforms;
    const std::vector<MaterialProbe::Float4> inputs{
        {.25f, .25f, 0, 0}, {.75f, .25f, 0, 0}, {.25f, .75f, 0, 0}, {.75f, .75f, 0, 0}, {.5f, .5f, 0, 0}};
    const auto* code = static_cast<const std::uint8_t*>(cs.Data());
    MaterialProbe::ComputeReadback gpu;
    const auto values =
        gpu.Run(std::vector<std::uint8_t>(code, code + cs.Size()), inputs, 1, 3, {tex}, {{true, true}}, bindings);
    for (std::size_t index = 0; index < inputs.size(); ++index)
    {
        MaterialProbe::Float4 expected{};
        const auto component = [&](unsigned channel) {
            float total = 0;
            for (std::size_t sample = 0; sample < (index == 4 ? 4u : 1u); ++sample)
            {
                const auto& pixel = tex.pixels[index == 4 ? sample : index];
                const float color[]{pixel.x, pixel.y, pixel.z, pixel.w};
                const auto encoded = std::round(color[channel] * 255.0f) / 255.0f;
                total += channel == 3 ? encoded : Decode(encoded);
            }
            return total / (index == 4 ? 4.0f : 1.0f);
        };
        expected = {component(0) * .5f, component(1) * .25f, component(2) * 2.0f, .25f};
        const auto actual = values[index * 3];
        // Hardware SRGB conversion has finite precision; blue is scaled by 2.
        // The separate corner-average check below verifies filtering order.
        Check(std::abs(actual.x - expected.x) < .006f && std::abs(actual.y - expected.y) < .006f &&
                  std::abs(actual.z - expected.z) < .006f && actual.w == .25f,
              "Hardware SRGB decode before filtering, sample=" + std::to_string(index) + " actual=" +
                  std::to_string(actual.x) + "," + std::to_string(actual.y) + "," + std::to_string(actual.z) + "," +
                  std::to_string(actual.w) + " expected=" + std::to_string(expected.x) + "," +
                  std::to_string(expected.y) + "," + std::to_string(expected.z) + "," + std::to_string(expected.w));
        const auto vector = values[index * 3 + 1];
        Check(vector.x == 4 && vector.y == 5 && vector.z == 6 && vector.w == -9, "GPU float3/int32 reflected packing");
        Check(values[index * 3 + 2].x == 0 && std::abs(values[index * 3 + 2].w - component(3)) < .003f,
              "GPU bool32 and linear alpha");
    }
    MaterialProbe::Float4 average{};
    for (std::size_t index = 0; index < 4; ++index)
    {
        average.x += values[index * 3].x * .25f;
        average.y += values[index * 3].y * .25f;
        average.z += values[index * 3].z * .25f;
    }
    Check(std::abs(values[12].x - average.x) < .006f && std::abs(values[12].y - average.y) < .006f &&
              std::abs(values[12].z - average.z) < .006f && std::abs(values[12].z - Decode(.5f) * 2.0f) > .1f,
          "Hardware bilinear sample averages decoded texels, including hardware conversion/filter precision");
    std::ofstream gpuEvidence(directory / "gpu.csv");
    gpuEvidence << "sample,field,x,y,z,w\n";
    for (std::size_t index = 0; index < values.size(); ++index)
        gpuEvidence << index / 3 << ',' << index % 3 << ',' << values[index].x << ',' << values[index].y << ','
                    << values[index].z << ',' << values[index].w << '\n';
    Check(gpuEvidence.good(), "Save actual SRGB/uniform readback");
    auto wrongTexture = texture;
    wrongTexture.texture.format = RHIFormat::RGBA8Unorm;
    const auto previousBytes = packet.uniforms;
    Check(!PrepareResources(layout, overrides, std::span(&wrongTexture, 1), packet, diagnostics) &&
              packet.uniforms == previousBytes,
          "Reject wrong encoding without replacing resource packet");
    wrongTexture.linearStorage = true;
    for (const auto format : {RHIFormat::R32Uint, RHIFormat::RGBA8Uint})
    {
        wrongTexture.texture.format = format;
        Check(!PrepareResources(layout, overrides, std::span(&wrongTexture, 1), packet, diagnostics) &&
                  diagnostics.back().code == "product.texture" && packet.uniforms == previousBytes,
              "Reject integer SRV behind generated Texture2D<float4> sampling");
    }
    auto wrongValues = overrides;
    wrongValues.push_back(wrongValues.front());
    Check(!PrepareResources(layout, wrongValues, std::span(&texture, 1), packet, diagnostics),
          "Duplicate override rejected");
    wrongValues = overrides;
    wrongValues[0].value = std::numeric_limits<double>::max();
    Check(!PrepareResources(layout, wrongValues, std::span(&texture, 1), packet, diagnostics),
          "Float32 overflow rejected");
    auto badReflection = reflection;
    for (auto& resource : badReflection.resources)
        if (resource.name == "lx_texture_0")
            resource.registerIndex = 17;
    Check(!ResolveBindings(program, badReflection, budget, layout, diagnostics), "Wrong reflected register rejected");
    Check(ResolveBindings(program, reflection, budget, layout, diagnostics), "Restore layout");
    std::vector<std::uint8_t> cooked;
    Check(WriteCookedProgram(verified, budget, cooked, error), "Write cooked bundle: " + error);
    auto reversedProduct = verified;
    std::ranges::reverse(reversedProduct.shaders);
    std::ranges::reverse(reversedProduct.targets);
    std::vector<std::uint8_t> reorderedCook;
    Check(WriteCookedProgram(reversedProduct, budget, reorderedCook, error) && reorderedCook == cooked,
          "Cook target order does not change artifact bytes");
    CookedProgram restored;
    Check(ReadCookedProgram(cooked, budget, restored, error) &&
              restored.product.program.semanticKey == verifiedIdentity &&
              restored.boundSource == BuildBoundSource(program) && restored.product.shaders.size() == 6,
          "Exact cooked round trip");
    Check(restored.product.layout == verified.layout &&
              restored.product.program.parameters == verified.program.parameters &&
              restored.product.program.resources == verified.program.resources &&
              restored.product.selection.route == verified.selection.route,
          "Cook restores typed defaults, encoding, reflected offsets and selected route");
    auto damaged = cooked;
    damaged[damaged.size() / 2] ^= 1;
    Check(!ReadCookedProgram(damaged, budget, restored, error) &&
              restored.product.program.semanticKey == verifiedIdentity,
          "Cook corruption preserves last accepted artifact");
    // Alter the serialized type's high byte while retaining a valid checksum.
    // A uint32 wire enum must not silently narrow back to a known uint8 type.
    auto unknownWireType = cooked;
    std::size_t typeOffset = 12;
    const auto skipText = [&]() {
        std::uint32_t length = 0;
        for (unsigned index = 0; index < 4; ++index)
            length |= static_cast<std::uint32_t>(unknownWireType.at(typeOffset + index)) << (8 * index);
        typeOffset += 4 + length;
    };
    for (unsigned index = 0; index < 4; ++index)
        skipText();
    typeOffset += 24; // features, surface, volume, sample count, tier, route
    skipText();       // selection reason
    typeOffset += 12; // parameter count and first parameter ID
    skipText();       // identifier
    skipText();       // display name
    unknownWireType.at(typeOffset + 1) = 1;
    std::uint64_t repairedChecksum = 14695981039346656037ull;
    for (const auto byte : std::span(unknownWireType).first(unknownWireType.size() - 8))
        repairedChecksum = (repairedChecksum ^ byte) * 1099511628211ull;
    for (unsigned index = 0; index < 8; ++index)
        unknownWireType[unknownWireType.size() - 8 + index] =
            static_cast<std::uint8_t>(repairedChecksum >> (8 * index));
    Check(!ReadCookedProgram(unknownWireType, budget, restored, error) &&
              restored.product.program.semanticKey == verifiedIdentity,
          "Unknown wire enum cannot truncate to a known socket type and replace the accepted generation");
    for (const auto length : {0u, 3u, 19u})
        Check(!ReadCookedProgram(std::span(cooked).first(length), budget, restored, error), "Truncated cook rejected");
    auto duplicateProduct = verified;
    duplicateProduct.shaders.push_back(verified.shaders.front());
    Check(!WriteCookedProgram(duplicateProduct, budget, damaged, error), "Duplicate backend/entry rejected");
    tight = budget;
    tight.compiledBytes = 1;
    Check(!WriteCookedProgram(verified, tight, damaged, error), "Compiled byte budget enforced");
    PipelineSlot slot;
    PipelineCache cache;
    RHIGraphicsPipelineDesc desc;
    desc.layout = {1};
    desc.numRenderTargets = 1;
    desc.rtvFormats[0] = RHIFormat::RGBA16Float;
    Check(slot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides, std::span(&texture, 1),
                       desc, {42}, diagnostics),
          "Publish owning PSO/route/layout/resource generation");
    const auto accepted = slot.Active();
    PipelineSlot cookedSlot;
    RHIShaderCompiler::ResetStats();
    Check(cookedSlot.Publish(cache, restored.product, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                             std::span(&texture, 1), desc, {42}, diagnostics) &&
              cookedSlot.Active()->semanticKey == verifiedIdentity && RHIShaderCompiler::GetStats().compiles == 0,
          "Cooked generation creates a real PSO without runtime Slang compilation/reflection");
    const auto creationsBeforeFailure = cache.creations;
    cache.fail = true;
    Check(!slot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides, std::span(&texture, 1),
                        desc, {42}, diagnostics) &&
              slot.Active() == accepted && cache.retired.empty(),
          "PSO failure retains generation");
    cache.fail = false;
    Check(!slot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                        std::span(&wrongTexture, 1), desc, {42}, diagnostics) &&
              slot.Active() == accepted && cache.creations == creationsBeforeFailure + 1,
          "Binding rejection precedes PSO creation");
    auto corruptProduct = verified;
    const std::array<std::uint8_t, 16> corruptCode{};
    const auto corruptShader = std::ranges::find_if(corruptProduct.shaders, [](const auto& shader) {
        return shader.backend == "dxil" && shader.entryPoint == "PSMain";
    });
    corruptShader->bytecode.assign(corruptCode.begin(), corruptCode.end());
    Check(!slot.Publish(cache, corruptProduct, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                        std::span(&texture, 1), desc, {42}, diagnostics) &&
              slot.Active() == accepted && cache.retired.empty(),
          "Actual D3D12 PSO rejection retains accepted generation");
    Check(slot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides, std::span(&texture, 1),
                       desc, {42}, diagnostics) &&
              slot.Active()->generation == 2 && cache.retired.size() == 1 && cache.retired.front().second.value == 42,
          "Accepted generation retires only prior handle at completion fence");
    PipelineSlot sharedSlot;
    cache.same = true;
    cache.retired.clear();
    Check(sharedSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                             std::span(&texture, 1), desc, {44}, diagnostics) &&
              sharedSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                                 std::span(&texture, 1), desc, {44}, diagnostics) &&
              cache.retired.empty(),
          "Same cache handle is not invalidated");
    PipelineSlot lifetimeSlot;
    auto retiringTexture = texture;
    auto retiringOwner = std::make_shared<const int>(91);
    const std::weak_ptr<const int> lifetime = retiringOwner;
    retiringTexture.owner = retiringOwner;
    retiringOwner.reset();
    Check(lifetimeSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                               std::span(&retiringTexture, 1), desc, {50}, diagnostics),
          "Publish retirement owner");
    retiringTexture.owner = owner;
    Check(lifetimeSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                               std::span(&retiringTexture, 1), desc, {50}, diagnostics) &&
              !lifetime.expired() && lifetimeSlot.RetiredGenerationCount() == 1,
          "Keep replaced texture owner alive through the submitted graphics fence, including reused PSOs");
    lifetimeSlot.CollectRetired({49});
    Check(!lifetime.expired(), "Incomplete graphics fence retains texture owner");
    lifetimeSlot.CollectRetired({50});
    Check(lifetime.expired() && lifetimeSlot.RetiredGenerationCount() == 0,
          "Completed graphics fence releases replaced texture owner");
    auto unknownOwner = std::make_shared<const int>(92);
    const std::weak_ptr<const int> unknownLifetime = unknownOwner;
    retiringTexture.owner = unknownOwner;
    unknownOwner.reset();
    Check(lifetimeSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                               std::span(&retiringTexture, 1), desc, {}, diagnostics),
          "Publish unknown completion owner");
    retiringTexture.owner = owner;
    Check(lifetimeSlot.Publish(cache, verified, RHIShaderBinary::Dxil, capabilities, budget, overrides,
                               std::span(&retiringTexture, 1), desc, {}, diagnostics),
          "Replace with unknown completion");
    lifetimeSlot.CollectRetired({UINT64_MAX});
    Check(!unknownLifetime.expired(), "Zero completion fence retains replaced texture until device-idle destruction");
    auto combinedVolume = verified;
    combinedVolume.program.volume = true;
    combinedVolume.program.features |= 0x2000u;
    combinedVolume.selection.tier = Tier::Special;
    auto volumeCapabilities = capabilities;
    volumeCapabilities.volume = true;
    const auto lastSurface = slot.Active();
    Check(!slot.Publish(cache, combinedVolume, RHIShaderBinary::Dxil, volumeCapabilities, budget, overrides,
                        std::span(&texture, 1), desc, {60}, diagnostics) &&
              slot.Active() == lastSurface,
          "Surface and Volume cannot publish a graphics-only generation that silently drops composition");
    std::ofstream bundle(directory / "fixture.lxmaterial", std::ios::binary);
    bundle.write(reinterpret_cast<const char*>(cooked.data()), static_cast<std::streamsize>(cooked.size()));
    Check(bundle.good(), "Save cooked evidence");
    bundle.close();
    namespace ck = experiment::cooked;
    experiment::AssetId graphId, textureId;
    Check(experiment::TryParseCanonicalAssetId("11111111-1111-4111-8111-111111111111", graphId) &&
              experiment::TryParseCanonicalAssetId("22222222-2222-4222-8222-222222222222", textureId),
          "Canonical asset identities");
    asset.blackboard[2].value = Uuid::ToString(textureId.value);
    const auto assetProgram = GenerateMaterialSlang(asset);
    Check(assetProgram.has_value(), "Generate canonical source graph");
    const auto assetHost = directory / "asset-bound.slang";
    std::ofstream assetSource(assetHost, std::ios::binary | std::ios::trunc);
    assetSource << BuildBoundSource(*assetProgram) << R"(
[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    return float4(id == 0 ? -1 : 1, id == 2 ? -1 : 1, 0, 1);
}
[shader("fragment")]
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    LXMaterialContext context;
    context.uv = float3(position.xy / 64.0, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    MaterialInputs material = LXGenerateMaterial(context, LXBoundMaterialParameters()).surfaceInputs;
    return float4(material.baseColor, material.roughness);
}
)";
    assetSource.close();
    const std::vector<CompileTarget> assetTargets{{RHIShaderBinary::Dxil, "VSMain", "vs_6_0"},
                                                  {RHIShaderBinary::Dxil, "PSMain", "ps_6_0"},
                                                  {RHIShaderBinary::SpirV, "VSMain", "vs_6_0"},
                                                  {RHIShaderBinary::SpirV, "PSMain", "ps_6_0"}};
    VerifiedProduct assetVerified;
    Check(VerifyProduct(*assetProgram, assetHost, assetTargets, permutation, options, capabilities, budget,
                        assetVerified, diagnostics),
          "Verify source graph host");
    ck::MaterialProgramCookProduct cookProduct;
    Check(ck::BuildMaterialProgramCookProduct(graphId, asset, assetVerified, budget, cookProduct, error),
          "Build canonical graph cook product");
    const auto publishedBytes = cookProduct.artifactBytes;
    Check(!ck::BuildMaterialProgramCookProduct(graphId, asset, verified, budget, cookProduct, error) &&
              cookProduct.artifactBytes == publishedBytes,
          "Different graph specialization rejected before publication");
    ck::CookedAssetManifest manifest;
    manifest.entries.push_back(cookProduct.manifestEntry);
    ck::CookedAssetManifestEntry textureEntry;
    textureEntry.assetId = textureId;
    textureEntry.kind = ck::CookedAssetKind::Texture;
    textureEntry.formatVersion = ck::kTextureArtifactVersion;
    textureEntry.byteSize = 1;
    textureEntry.artifactPath = ck::MakeDerivedTextureArtifactPath(textureId, ".png");
    const std::array<std::byte, 1> textureBytes{std::byte{1}};
    Check(ck::ComputeSha256(textureBytes, textureEntry.contentSha256, error), "Hash texture manifest fixture");
    manifest.entries.push_back(textureEntry);
    manifest.sourceAssets = {{graphId, "Materials/fixture.shadergraph"}, {textureId, "Textures/fixture.png"}};
    const auto manifestBytes = ck::WriteAssetManifest(manifest);
    Check(manifestBytes.Succeeded(), "Publish graph/texture manifest and source identities");
    std::vector<ck::AssetManifestIssue> manifestIssues;
    const auto catalog = ck::CookedAssetCatalog::Load(manifestBytes.bytes, directory, manifestIssues);
    Check(manifestIssues.empty() && catalog.CountOfKind(ck::CookedAssetKind::MaterialProgram) == 1,
          "Read product asset catalog");
    std::vector<experiment::AssetId> closure;
    Check(catalog.CollectClosure(graphId, closure, error) && closure.size() == 2 && closure.back() == graphId,
          "Graph dependency closure loads texture before material program");
    const auto artifactFile = directory / cookProduct.manifestEntry.artifactPath;
    std::filesystem::create_directories(artifactFile.parent_path());
    std::ofstream artifact(artifactFile, std::ios::binary | std::ios::trunc);
    artifact.write(reinterpret_cast<const char*>(publishedBytes.data()),
                   static_cast<std::streamsize>(publishedBytes.size()));
    artifact.close();
    ck::LooseArtifactByteSource loose(directory);
    CookedProgram catalogProduct;
    Check(catalog.OpenMaterialProgram(graphId, loose, budget, catalogProduct, error) &&
              catalogProduct.product.program.semanticKey == assetVerified.program.semanticKey,
          "GUID-based loose artifact load verifies SHA-256 and exact typed generation");
    auto badEntry = cookProduct.manifestEntry;
    badEntry.contentSha256[0] ^= 1;
    Check(!ck::OpenCookedMaterialProgram(badEntry, loose, budget, catalogProduct, error) &&
              catalogProduct.product.program.semanticKey == assetVerified.program.semanticKey,
          "SHA-256 failure preserves accepted runtime generation");
    badEntry = cookProduct.manifestEntry;
    badEntry.dependencies.clear();
    Check(!ck::OpenCookedMaterialProgram(badEntry, loose, budget, catalogProduct, error),
          "Cooked texture table cannot disagree with manifest dependencies");
    auto missingTexture = manifest;
    missingTexture.entries.pop_back();
    const auto missingBytes = ck::WriteAssetManifest(missingTexture);
    Check(!missingBytes.Succeeded(), "Manifest rejects missing material texture dependency");
    std::ofstream manifestEvidence(directory / "asset-manifest.cemf", std::ios::binary | std::ios::trunc);
    manifestEvidence.write(reinterpret_cast<const char*>(manifestBytes.bytes.data()),
                           static_cast<std::streamsize>(manifestBytes.bytes.size()));
    Check(manifestEvidence.good(), "Save asset catalog evidence");
    Check(asset.Save(directory / "fixture.shadergraph", &error), "Save source graph cook fixture");
    std::ofstream meta(directory / "fixture.shadergraph.meta");
    meta << "guid: " << Uuid::ToString(graphId.value) << '\n';
    const auto pakFile = directory / "material-program.pak";
    Pak::BuildOptions pakOptions;
    pakOptions.compress = false;
    pakOptions.encrypt = true;
    Pak::Builder pak(pakFile, pakOptions);
    pak.addMemory("Assets/Derived/asset-manifest.cemf", manifestBytes.bytes);
    pak.addMemory("Assets/" + cookProduct.manifestEntry.artifactPath, cookProduct.artifactBytes);
    pak.addMemory("Assets/" + textureEntry.artifactPath, textureBytes);
    pak.finish();
    auto archive = std::make_shared<const Pak::Archive>(pakFile, Pak::OpenOptions{.key = pak.key()});
    ck::PakAudioClipByteSource mounted(archive);
    CookedProgram pakProduct;
    Check(catalog.OpenMaterialProgram(graphId, mounted, budget, pakProduct, error) &&
              pakProduct.product.program.semanticKey == assetVerified.program.semanticKey &&
              pakProduct.product.layout == assetVerified.layout,
          "Encrypted mounted PAK restores the exact cooked generation without source parsing or shader compilation");
    VerifyMaterialRuntime(verified, catalog, loose, mounted, asset, graphId);
    std::cout << "LX_MATERIAL_PRODUCT_OK checks=" << checks << " compiled=" << shaders.size() + assetTargets.size()
              << " gpuComponents=60\n";
}

void VerifyCookedTree(const std::filesystem::path& root)
{
    namespace ck = experiment::cooked;
    const auto manifestFile = root / "Derived/asset-manifest.cemf";
    std::ifstream input(manifestFile, std::ios::binary);
    const std::vector<char> raw{std::istreambuf_iterator<char>(input), {}};
    Check(input.is_open() && !input.bad() && !raw.empty(), "Read actual AssetCooker manifest");
    std::vector<ck::AssetManifestIssue> issues;
    const auto catalog =
        ck::CookedAssetCatalog::Load({reinterpret_cast<const std::byte*>(raw.data()), raw.size()}, root, issues);
    Check(issues.empty() && catalog.CountOfKind(ck::CookedAssetKind::MaterialProgram) == 1 &&
              catalog.CountOfKind(ck::CookedAssetKind::Texture) == 1,
          "Actual cooked catalog kinds");
    const ck::LooseArtifactByteSource loose(root);
    Budget budget;
    std::string error;
    std::string key;
    for (const auto& entry : catalog.Entries())
    {
        if (entry.kind != ck::CookedAssetKind::MaterialProgram)
            continue;
        CookedProgram program;
        Check(catalog.OpenMaterialProgram(entry.assetId, loose, budget, program, error), "Load actual cooked material");
        key = program.product.program.semanticKey;
        Capabilities capabilities;
        capabilities.coreForward = true;
        auto owner = std::make_shared<const int>(10);
        const TextureBinding texture{0, {{1}, RHIFormat::RGBA8UnormSrgb, 1, 1, 1, 1, false}, owner};
        RHIGraphicsPipelineDesc description;
        description.layout = {1};
        description.numRenderTargets = 1;
        description.rtvFormats[0] = RHIFormat::RGBA16Float;
        PipelineCache cache;
        PipelineSlot slot;
        std::vector<LXMaterialDiagnostic> diagnostics;
        RHIShaderCompiler::ResetStats();
        Check(slot.Publish(cache, program.product, RHIShaderBinary::Dxil, capabilities, budget, {},
                           std::span(&texture, 1), description, {}, diagnostics) &&
                  RHIShaderCompiler::GetStats().compiles == 0,
              "Actual cooker output creates native PSO without Slang");
    }
    Check(!key.empty(), "Accepted cooked identity");
    const auto packedFile = root / "verified.pak";
    Pak::BuildOptions options;
    options.encrypt = true;
    options.compress = false;
    Pak::Builder pak(packedFile, options);
    pak.addFile("Assets/Derived/asset-manifest.cemf", manifestFile);
    for (const auto& entry : catalog.Entries())
        pak.addFile("Assets/" + entry.artifactPath, root / entry.artifactPath);
    pak.finish();
    auto archive = std::make_shared<const Pak::Archive>(packedFile, Pak::OpenOptions{.key = pak.key()});
    const ck::PakAudioClipByteSource mounted(archive);
    for (const auto& entry : catalog.Entries())
    {
        if (entry.kind != ck::CookedAssetKind::MaterialProgram)
            continue;
        CookedProgram program;
        Check(catalog.OpenMaterialProgram(entry.assetId, mounted, budget, program, error) &&
                  program.product.program.semanticKey == key,
              "Actual encrypted PAK material load");
    }
    std::cout << "LX_MATERIAL_COOKED_PRODUCT_OK checks=" << checks << " compilerCalls=0\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 3 && std::string_view(argv[1]) == "--cooked-root")
        {
            VerifyCookedTree(argv[2]);
            return 0;
        }
        Check(argc == 2, "Expected repository path");
        Run(std::filesystem::path(argv[1]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_PRODUCT_FAILED " << error.what() << '\n';
        return 1;
    }
}
