#include "material_owner_checks.h"
#include "support/MaterialPipelineSlot.h"
#include "MaterialGraphRenderBindings.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using namespace material_graph;
using namespace LX;
std::size_t checks{};

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

Id Pin(const LXGraph& graph, Id node, const std::string& name, Direction direction)
{
    for (const auto& pin : graph.FindNode(node)->pins)
        if (pin.Identifier() == name && pin.direction == direction)
            return pin.id;
    throw std::runtime_error("Missing fixture pin: " + name);
}

void Connect(LXGraph& graph, Id from, const std::string& output, Id to, const std::string& input)
{
    Check(
        graph.Connect(Pin(graph, from, output, Direction::Output), Pin(graph, to, input, Direction::Input)).has_value(),
        "Fixture connection");
}

VerifiedProduct CompileFixture(const std::filesystem::path& root)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    const auto roughness = asset.CreateNode("LXParameterFloat", -100, 100);
    asset.activeOutput = output;
    asset.blackboard = {{900, "roughness", "Roughness", PinType::Float, .31}};
    Check(asset.graph.SetSocketValue(Pin(asset.graph, surface, "Emission Strength", Direction::Input), 1.0),
          "Keep both emission sample inputs active");
    Check(asset.graph.SetProperty(roughness, "parameter", "900"), "Fixture parameter");
    Connect(asset.graph, roughness, "Value", surface, "Roughness");
    Connect(asset.graph, surface, "BSDF", output, "Surface");
    for (std::size_t pair = 0; pair < 2; ++pair)
    {
        const auto multiply = asset.CreateNode("LXMultiplyColor", -200, 0);
        for (std::size_t side = 0; side < 2; ++side)
        {
            const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
            Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"), "Texture GUID");
            const std::string interpolation = side ? "Closest" : "Linear";
            const std::string extension = pair == side ? "REPEAT" : "EXTEND";
            asset.graph.SetProperty(image, "interpolation", interpolation);
            asset.graph.SetProperty(image, "extension", extension);
            Check(asset.graph.FindNode(image)->properties.at("interpolation") == interpolation, "Interpolation");
            Check(asset.graph.FindNode(image)->properties.at("extension") == extension, "Addressing");
            Connect(asset.graph, image, "Color", multiply, side ? "B" : "A");
        }
        Connect(asset.graph, multiply, "Result", surface, pair ? "Emission Color" : "Base Color");
    }
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto generated = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!generated, "Generate four independent samplers");
    const auto file = root / "Build/Obj/MaterialProductProbe/render-bindings.slang";
    std::ofstream source(file, std::ios::binary | std::ios::trunc);
    source << BuildBoundSource(*generated) << R"(
[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(vertices[id], 0, 1);
}
[shader("fragment")]
float4 PSMain(float4 position : SV_Position) : SV_Target
{
    const float coordinates[4] = {0.375, 1.25, -0.25, 0.75};
    LXMaterialContext context;
    context.uv = float3(coordinates[uint(position.x)], 0.5, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    MaterialInputs material = LXGenerateMaterial(context, LXBoundMaterialParameters()).surfaceInputs;
    return float4(material.baseColor.r, material.emissionColor.g, material.roughness, material.baseColor.b);
}
)";
    source.close();
    RHIShaderCompileOptions options;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    const CompileTarget targets[] = {{RHIShaderBinary::Dxil, "VSMain", "vs_6_0"},
                                     {RHIShaderBinary::Dxil, "PSMain", "ps_6_0"},
                                     {RHIShaderBinary::SpirV, "VSMain", "vs_6_0"},
                                     {RHIShaderBinary::SpirV, "PSMain", "ps_6_0"}};
    VerifiedProduct product;
    Check(VerifyProduct(*generated, file, targets, {}, options, {.coreForward = true}, {}, product, diagnostics),
          diagnostics.empty() ? "Verify product bindings" : diagnostics.front().message);
    Check(product.layout.textures.size() == 1 && product.layout.samplers.size() == 4, "One image, four sampler slots");
    return product;
}

float Decode(float encoded)
{
    return encoded <= .04045f ? encoded / 12.92f : std::pow((encoded + .055f) / 1.055f, 2.4f);
}

float Sample(float u, std::size_t channel, bool linear, bool repeat)
{
    const std::array<std::array<unsigned, 3>, 2> values{{{128, 64, 32}, {255, 192, 160}}};
    const auto texel = [&](int index) {
        index = repeat ? (index % 2 + 2) % 2 : std::clamp(index, 0, 1);
        return Decode(float(values[index][channel]) / 255.f);
    };
    if (!linear)
        return texel(int(std::floor(u * 2.f)));
    const float coordinate = u * 2.f - .5f;
    const int first = int(std::floor(coordinate));
    return std::lerp(texel(first), texel(first + 1), coordinate - float(first));
}

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const auto product = CompileFixture(root);
    GenerationStore store;
    experiment::AssetId graph;
    Check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graph.value), "Graph identity");
    std::string error;
    const auto generation = store.Load(
        graph,
        [&](CookedProgram& cooked, std::string&) {
            cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
            return true;
        },
        false, error);
    Check(!!generation, "Owning verified generation: " + error);
    auto image = TextureImage::Allocate(RHIFormat::RGBA8UnormSrgb, 2, 1, 1, 1);
    const std::array<std::uint8_t, 8> pixels{128, 64, 32, 255, 255, 192, 160, 255};
    std::memcpy(image.MutablePixelsAt(*image.Find(0, 0)), pixels.data(), pixels.size());
    const auto texture = Texture::CreateSharedFromImage("LX.RenderBindings.SRGB", std::move(image));
    Check(!!texture, "Actual CPU texture");
    own::shared_owner<const Instance> instance;
    Check(BuildInstance(
              generation, {graph, {{900, .31}}, {}},
              [&](const experiment::AssetId&, LXColorSpace, std::string&) { return texture; }, instance, error),
          "Build owning instance: " + error);

    DX12DeviceResources device;
    Check(device.Initialize(4, 1, error), "Actual DX12 device: " + error);
    Check(device.HasDebugMessageQueue(), "D3D12 validation is installed");
    DX12RootSignatureCache roots;
    DX12PSOManager pipelines;
    DX12TextureCache textures;
    Check(roots.Initialize(&device, error), "Actual root signature cache");
    Check(pipelines.Initialize(&device, L"", error), "Actual pipeline cache: " + error);
    Check(textures.Initialize(&device, error), "Actual texture cache: " + error);
    PassLayout layout;
    const RHIPipelineLayoutParam host[] = {RHILayout::Cbv(0)};
    Check(CreatePassLayout(roots, product.layout, host, {}, false, layout, error),
          "Append independent material root ranges");
    Check(layout.uniformSlot == 1u && layout.textureSlot == 2u && layout.samplerSlot == 3u,
          "Exact appended root slots");
    const auto acceptedLayout = layout.handle;
    for (const auto conflict : {RHILayout::Cbv(2), RHILayout::Constants(2, 1), RHILayout::Srv(16),
                                RHILayout::SrvTable(3, 15), RHILayout::SamplerTable(5, 1)})
        Check(!CreatePassLayout(roots, product.layout, {&conflict, 1}, {}, false, layout, error) &&
                  layout.handle == acceptedLayout,
              "Conflicting host range preserves accepted layout");
    const RHIStaticSamplerDesc staticConflict{RHISampler::Point(), 4};
    Check(!CreatePassLayout(roots, product.layout, {}, {&staticConflict, 1}, false, layout, error) &&
              layout.handle == acceptedLayout,
          "Static sampler overlap rejection");
    auto invalidLayout = product.layout;
    invalidLayout.samplers.front().slot = 64;
    Check(!CreatePassLayout(roots, invalidLayout, {}, {}, false, layout, error) && layout.handle == acceptedLayout,
          "Out-of-range sampler slot retains accepted layout");
    invalidLayout = product.layout;
    invalidLayout.samplers.push_back(invalidLayout.samplers.front());
    Check(!CreatePassLayout(roots, invalidLayout, {}, {}, false, layout, error) && layout.handle == acceptedLayout,
          "Duplicate sampler slot retains accepted layout");

    RHITextureHandle target;
    RHITextureDesc targetDescription;
    targetDescription.width = 4;
    targetDescription.height = 1;
    targetDescription.format = RHIFormat::RGBA32Float;
    targetDescription.allowRenderTarget = true;
    targetDescription.debugName = L"LX.MaterialBinding.Target";
    Check(device.CreateTexture(targetDescription, target, error), "Actual render target");
    RHIReadback readback;
    Check(device.CreateReadback(4, 1, targetDescription.format, 1, readback, error), "Actual readback");
    RenderBindingCache bindings;
    own::shared_owner<const RenderBindings> packet;
    Check(!bindings.Prepare(device, textures, instance, layout, packet, error) && !packet,
          "No upload outside recording");
    PipelineSlot pipeline;
    std::size_t gpuComponents = 0;
    RHIResourceState targetState = RHIResourceState::Common;
    for (unsigned frame = 0; frame < 4; ++frame)
    {
        Check(device.BeginFrame(error), "Actual BeginFrame");
        textures.BeginFrame(frame);
        auto& encoder = device.GetImmediateEncoder();
        if (packet)
        {
            Check(!RenderBindingCache::Bind(device, encoder, RHIBindPoint::Graphics, *packet, error),
                  "Reject stale-frame packet");
            Check(!RenderBindingCache::ValidatePass(device, *packet, layout, error),
                  "Pass binding rejects stale recordings");
        }
        // A prepared lookup is weak: after the previous recording's callers
        // release their packet, it cannot keep the CPU wrapper resident.
        own::weak_owner<const RenderBindings> previousPacket(packet);
        packet.reset();
        Check(previousPacket.expired(), "Prepared weak lookup does not own an unused binding packet");
        const double roughness = frame % 2 ? .81 : .31;
        Check(BuildInstance(
                  generation, {graph, {{900, roughness}}, {}},
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return texture; }, instance, error),
              "New immutable instance values");
        Check(bindings.Prepare(device, textures, instance, layout, packet, error),
              "Actual RHI material prepare: " + error);
        Check(bindings.SamplerTableCount() == 1 && packet->resources.samplers.size() == 4,
              "Sampler table is shared across repeated frames and value edits");
        const auto accepted = packet;
        own::shared_owner<const RenderBindings> repeated;
        Check(bindings.Prepare(device, textures, instance, layout, repeated, error) && material_graph_test::SamePinnedObject(repeated, packet),
              "Same immutable instance and complete layout reuse one packet in this recording");
        Check(packet->instancePins && packet->instancePinIndex != InstanceFramePins::InvalidIndex &&
                  material_graph_test::SamePinnedObject(packet->instance, instance) &&
                  material_graph_test::SamePinnedObject(packet->instancePins->Borrow(packet->instancePinIndex), instance) &&
                  packet->resources.owners.empty(),
              "Binding packet borrows the exact instance and texture closure through its frame pin index");
        auto otherViewPins = own::make_shared<InstanceFramePins>();
        otherViewPins->Retain(instance);
        Check(bindings.Prepare(device, textures, instance, layout, repeated, error, otherViewPins) &&
                  material_graph_test::SamePinnedObject(repeated, packet) &&
                  !material_graph_test::SamePinnedObject(repeated->instancePins, otherViewPins),
              "Same-recording reuse retains its original independent frame pin table");
        Check(RenderBindingCache::ValidatePass(device, *packet, layout, error),
              "Second pass validates the shared material resources without copying");
        auto wrongLayout = layout;
        wrongLayout.samplerSlot.reset();
        Check(!RenderBindingCache::ValidatePass(device, *packet, wrongLayout, error),
              "Pass binding rejects a missing reflected slot");
        Check(!bindings.Prepare(device, textures, instance, wrongLayout, packet, error) && material_graph_test::SamePinnedObject(packet, accepted),
              "Missing sampler root retains accepted render packet");
        wrongLayout = layout;
        wrongLayout.samplerSlot = layout.uniformSlot;
        Check(!bindings.Prepare(device, textures, instance, wrongLayout, packet, error) && material_graph_test::SamePinnedObject(packet, accepted),
              "Aliased root slots retain accepted render packet");
        Instance wrongInstanceValue(*instance);
        wrongInstanceValue.uniforms.clear();
        const auto wrongInstance = own::make_shared<const Instance>(std::move(wrongInstanceValue));
        Check(!bindings.Prepare(device, textures, wrongInstance, layout, packet, error, accepted->instancePins) &&
                  material_graph_test::SamePinnedObject(packet, accepted),
              "Copied representation identity cannot substitute a different object in the frame pin table");
        Check(!bindings.Prepare(device, textures, wrongInstance, layout, packet, error) && material_graph_test::SamePinnedObject(packet, accepted),
              "Invalid packed values retain accepted render packet");
        if (frame == 0)
        {
            Instance missingPixelsValue(*instance);
            missingPixelsValue.textures.front().owner = own::make_shared<const Texture>();
            const auto missingPixels = own::make_shared<const Instance>(std::move(missingPixelsValue));
            Check(!bindings.Prepare(device, textures, missingPixels, layout, packet, error) && material_graph_test::SamePinnedObject(packet, accepted) &&
                      textures.GetUploadFailureCount() == 1,
                  "Actual cache neutral substitution is rejected and retains the accepted packet");
        }
        RHIGraphicsPipelineDesc description;
        description.layout = layout.handle;
        description.rtvFormats[0] = targetDescription.format;
        std::vector<TextureBinding> uploaded;
        for (const auto& owner : instance->textures)
            uploaded.push_back({owner.slot, textures.GetOrUpload((owner.owner ? &*owner.owner.borrow() : nullptr), owner.owner ? owner.owner->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), owner.owner});
        std::vector<LXMaterialDiagnostic> diagnostics;
        Check(pipeline.Publish(pipelines, product, RHIShaderBinary::Dxil, {.coreForward = true}, {},
                               instance->description.parameters, uploaded, description, {}, diagnostics),
              "Actual PSO publication");
        encoder.SetPipeline(RHIBindPoint::Graphics, pipeline.Active()->pipeline.GetHandle());
        const std::array<float, 4> dummy{};
        encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, device.UploadConstants(dummy.data(), sizeof(dummy)));
        Check(RenderBindingCache::Bind(device, encoder, RHIBindPoint::Graphics, *packet, error),
              "Actual material root binding");
        const RHITransition before{target, targetState, RHIResourceState::RenderTarget};
        encoder.ResourceBarriers({{&before, 1}});
        const auto renderTarget = device.CreateRenderTargets(std::span(&target, 1));
        Check(renderTarget.IsValid(), "Actual RTV descriptors");
        encoder.BindRenderTargets(renderTarget);
        encoder.SetViewportAndScissor(4, 1);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.Draw(3, 1);
        const RHITransition after{target, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
        encoder.ResourceBarriers({{&after, 1}});
        targetState = RHIResourceState::CopySource;
        encoder.CopyToReadback(readback, target);
        Check(device.EndFrame(error), "Actual submit: " + error);
        device.WaitForGpu();
        RHIReadbackImage values;
        Check(device.MapReadback(readback, values, error), "Actual GPU readback");
        const std::array<float, 4> coordinates{.375f, 1.25f, -.25f, .75f};
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            const float u = coordinates[x];
            const std::array<float, 4> expected{Sample(u, 0, true, true) * Sample(u, 0, false, false),
                                                Sample(u, 1, true, false) * Sample(u, 1, false, true), float(roughness),
                                                Sample(u, 2, true, true) * Sample(u, 2, false, false)};
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                const float actual = values.At(x, 0, channel);
                Check(std::isfinite(actual) && std::abs(actual - expected[channel]) < .001f,
                      "Actual sampler/address/SRGB/uniform GPU result: pixel=" + std::to_string(x) +
                          " channel=" + std::to_string(channel) + " actual=" + std::to_string(actual) +
                          " expected=" + std::to_string(expected[channel]));
                ++gpuComponents;
            }
        }
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "D3D12 validation: " + messages);
    }
    Check(textures.GetUploadFailureCount() == 1 && textures.GetStats().uploads == 1,
          "Only the injected missing-pixels upload failed; valid texture upload was reused");
    device.ReleaseReadback(readback);
    device.ReleaseTexture(target);
    packet.reset();
    bindings.Clear();
    textures.Shutdown();
    pipelines.Shutdown();
    roots.Shutdown();
    device.Shutdown();
    std::cout << "LX_MATERIAL_RENDER_BINDINGS_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " samplerSlots=4 samplerTables=1 frames=4\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2)
            throw std::runtime_error("Expected repository root.");
        Run(std::filesystem::absolute(argv[1]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_RENDER_BINDINGS_FAILED " << error.what() << '\n';
        return 1;
    }
}
