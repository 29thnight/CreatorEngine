#include "material_owner_checks.h"
#include "support/MaterialGraphScenePacket.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"

#include <cmath>
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
using namespace material_graph;
using namespace LX;
std::size_t checks{}, gpuComponents{};

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

Id Pin(const LXGraph& graph, Id node, const std::string& name, Direction direction)
{
    for (const auto& pin : graph.FindNode(node)->pins)
    {
        if (pin.Identifier() == name && pin.direction == direction)
        {
            return pin.id;
        }
    }
    throw std::runtime_error("Missing fixture pin: " + name);
}

void Connect(LXGraph& graph, Id from, const std::string& output, Id to, const std::string& input)
{
    Check(
        graph.Connect(Pin(graph, from, output, Direction::Output), Pin(graph, to, input, Direction::Input)).has_value(),
        "Fixture connection");
}

VerifiedProduct CompileFixture(const std::filesystem::path& root, bool layered)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    const auto parameter = asset.CreateNode("LXParameterFloat", -200, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{900, "ior", "IOR", PinType::Float, 1.3}};
    Check(asset.graph.SetProperty(parameter, "parameter", "900"), "IOR parameter");
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"), "Texture GUID");
    for (const auto& [name, value] :
         {std::pair{"Roughness", 0.0}, {"Coat Roughness", 0.0}, {"Coat Weight", layered ? .35 : 0.0}, {"Alpha", 1.0}})
    {
        const auto pin = Pin(asset.graph, surface, name, Direction::Input);
        asset.graph.SetSocketValue(pin, value);
        Check(asset.graph.FindPin(pin)->value == LXSocketValue{value}, name);
    }
    Connect(asset.graph, image, "Color", surface, "Base Color");
    Connect(asset.graph, parameter, "Value", surface, "IOR");
    Connect(asset.graph, surface, "BSDF", output, "Surface");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto generated = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!generated, "Generate owning Core/Layered program");
    const auto file = root / "Build/Obj/MaterialProductProbe" / (layered ? "scene-layered.slang" : "scene-core.slang");
    std::ofstream source(file, std::ios::binary | std::ios::trunc);
    source << BuildBoundSource(*generated) << R"(
#include "PrincipledIblLookup.slang"
#include "PbrCoverage.slang"
cbuffer SceneFixture : register(b0) { uint gCoverage; float gCutoff; float2 gPadding; };
StructuredBuffer<PrincipledIblBakeSample> gSceneIbl : register(t0);
[shader("vertex")]
float4 VSMain(uint id : SV_VertexID) : SV_Position
{
    const float2 vertices[3] = {float2(-1, -1), float2(-1, 3), float2(3, -1)};
    return float4(vertices[id], 0, 1);
}
[shader("fragment")]
float4 PSMain(float4 position : SV_Position, bool frontFace : SV_IsFrontFace) : SV_Target
{
    LXMaterialContext context;
    context.uv = float3(0.5, 0.5, 0);
    context.lod = 0;
    context.normal = float3(0, 0, 1);
    context.tangent = float3(1, 0, 0);
    context.bitangent = float3(0, 1, 0);
    const LXGeneratedMaterial inputs = LXGenerateMaterial(context, LXBoundMaterialParameters());
    const PrincipledSurface material = LXEvaluateGeneratedSurface(inputs, context);
    const float alpha = ApplyPbrCoverage(material.alpha, gCutoff, gCoverage, frontFace);
    const PrincipledIblBakeSample sample = gSceneIbl[0];
    const float3 ambient = EvaluateBakedPrincipledIbl(material, float3(0, 0, 1),
        (LX_MATERIAL_FEATURE_MASK & 0x0780u) != 0, sample);
    return float4(ambient.r, material.ior, sample.baseSingleAlbedo.r, alpha);
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
    const bool verified = VerifyProduct(*generated, file, targets, {}, options,
                                        {.coreForward = true, .layeredLookup = true}, {}, product, diagnostics);
    std::string messages;
    for (const auto& diagnostic : diagnostics)
    {
        messages += diagnostic.message + "\n";
    }
    Check(verified, diagnostics.empty() ? "Verify Scene fixture" : messages);
    Check(product.selection.route == Route::Forward &&
              product.selection.tier == (layered ? Tier::Layered : Tier::Standard),
          "Physical route is Forward for opaque IOR materials too");
    return product;
}

own::shared_owner<const Texture> TextureFixture(bool cube)
{
    auto image =
        TextureImage::Allocate(cube ? RHIFormat::RGBA32Float : RHIFormat::RGBA8UnormSrgb, 1, 1, cube ? 6 : 1, 1, cube);
    for (unsigned face = 0; face < (cube ? 6u : 1u); ++face)
    {
        auto* pixels = image.MutablePixelsAt(*image.Find(0, face));
        if (cube)
        {
            const std::array<float, 4> color{2, 1, .5f, 1};
            std::memcpy(pixels, color.data(), sizeof(color));
        }
        else
        {
            const std::array<std::uint8_t, 4> color{128, 128, 128, 255};
            std::memcpy(pixels, color.data(), sizeof(color));
        }
    }
    return Texture::CreateSharedFromImage(cube ? "LX.Scene.Environment" : "LX.Scene.Image", std::move(image));
}

struct QueueGate
{
    Microsoft::WRL::ComPtr<ID3D12Fence> fence;
    ~QueueGate() { Release(); }
    void Release()
    {
        if (fence)
        {
            fence->Signal(1);
        }
    }
};

struct Drain
{
    DX12DeviceResources& device;
    QueueGate& gate;
    SceneMaterialSlot& slot;
    ~Drain()
    {
        gate.Release();
        if (device.GetCurrentUploadRecordingId())
        {
            device.AbortFrame();
        }
        device.WaitForGpu();
        slot.ShutdownAfterIdle();
    }
};

// Inject a creation failure without corrupting bytecode or emitting an
// expected D3D12 error into the clean runtime-validation gate.
class RejectingCache final : public IRenderPipelineCache
{
  public:
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc&, std::string& error) override
    {
        error = "Injected Scene PSO creation failure";
        return {};
    }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc&, std::string&) override { return {}; }
    bool InvalidatePipeline(RHIPipelineHandle, RHICompletionPoint) override { return false; }
    std::uint32_t InvalidatePipelines(RHICompletionPoint) override { return 0; }
    std::uint32_t CollectRetiredPipelines(RHICompletionPoint) override { return 0; }
};

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const std::array products{CompileFixture(root, false), CompileFixture(root, true)};
    GenerationStore store;
    experiment::AssetId graph;
    Check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graph.value), "Graph ID");
    std::string error;
    const auto generation = [&](bool layered) {
        return store.Load(
            graph,
            [&](CookedProgram& cooked, std::string&) {
                const auto& product = products[layered ? 1 : 0];
                cooked = {product, WriteMaterialProgramMetadata(product.program), BuildBoundSource(product.program)};
                return true;
            },
            true, error);
    };
    const auto core = generation(false), layered = generation(true);
    Check(core && layered && !material_graph_test::SamePinnedObject(core, layered), "Immutable compiled generations");
    const auto texture = TextureFixture(false), environmentTexture = TextureFixture(true);
    const float base = std::pow((128.f / 255.f + .055f) / 1.055f, 2.4f);
    const auto evaluation = [&](float ior, bool coat, std::uint64_t view = 1) {
        SceneSurfaceEvaluation result;
        Check(BuildInstance(
                  coat ? layered : core, {graph, {{900, double(ior)}}, {}},
                  [&](const experiment::AssetId&, LXColorSpace, std::string&) { return texture; }, result.instance,
                  error),
              "Build Scene instance: " + error);
        result.sceneEpoch = 1;
        result.geometryRevision = 1;
        result.viewRevision = view;
        IblBakePoint point;
        point.baseAlpha = {base, base, base, 1};
        point.normalRoughness[3] = 0;
        point.metalIorLevelAo[1] = ior;
        point.coatWeightRoughIorFilmThickness = {coat ? .35f : 0, 0, 1.5f, 0};
        point.viewTier[3] = 1.f;
        result.points.push_back(point);
        return result;
    };

    DX12DeviceResources device;
    Check(device.Initialize(1, 1, error), "Native device: " + error);
    Check(device.HasDebugMessageQueue(), "Native validation queue");
    DX12RootSignatureCache roots;
    DX12PSOManager pipelines;
    DX12TextureCache textures;
    Check(roots.Initialize(&device, error), "Native root cache");
    Check(pipelines.Initialize(&device, L"", error), "Native pipeline cache");
    Check(textures.Initialize(&device, error), "Native texture cache");
    RenderBindingCache bindings;
    IblBaker baker;
    RHIShaderBlob bakeShader;
    const auto shaderFile = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang";
    for (const auto binary : {RHIShaderBinary::SpirV, RHIShaderBinary::Dxil})
    {
        RHIShaderCompiler::ScopedOutput output(binary);
        Check(RHIShaderCompiler::CompileFile(shaderFile.string(), "CSMain", "cs_6_0", bakeShader, error),
              "Compiled bake: " + error);
    }
    Check(baker.Initialize(device, roots, pipelines, bakeShader, error), "Native baker: " + error);
    std::array<ScenePassLayout, 2> layouts;
    const RHIPipelineLayoutParam host[] = {RHILayout::Cbv(0)};
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(CreateScenePassLayout(roots, products[i].layout, host, {}, false, layouts[i], error),
              "Scene pass layout");
    }
    const auto acceptedLayout = layouts[0].material.handle;
    const auto conflictingHost = RHILayout::Srv(0);
    Check(!CreateScenePassLayout(roots, products[0].layout, {&conflictingHost, 1}, {}, false, layouts[0], error) &&
              layouts[0].material.handle == acceptedLayout,
          "Host IBL conflict retains layout");

    std::array<RHITextureHandle, 2> targets;
    std::array<RHIReadback, 2> readbacks;
    RHITextureDesc description;
    description.width = description.height = 1;
    description.format = RHIFormat::RGBA32Float;
    description.allowRenderTarget = true;
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(device.CreateTexture(description, targets[i], error), "Native target");
        Check(device.CreateReadback(1, 1, description.format, 1, readbacks[i], error), "Native readback");
    }
    std::array targetStates{RHIResourceState::Common, RHIResourceState::Common};
    IblEnvironment environment;
    Check(device.BeginFrame(error), "Warm upload BeginFrame");
    textures.BeginFrame(0);
    const auto imageEntry = textures.GetOrUpload((texture ? &*texture.borrow() : nullptr), texture ? texture->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
    environment = {textures.GetOrUpload((environmentTexture ? &*environmentTexture.borrow() : nullptr), environmentTexture ? environmentTexture->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), 1, environmentTexture};
    Check(imageEntry.IsValid() && environment.cube.IsValid(), "Native owning textures");
    const RHITransition envTransition{environment.cube.handle, RHIResourceState::PixelShaderResource,
                                      RHIResourceState::ShaderResource};
    device.GetImmediateEncoder().ResourceBarriers({{&envTransition, 1}});
    Check(device.EndFrame(error), "Warm upload submission");
    device.WaitForGpu();

    {
        QueueGate gate;
        SceneMaterialSlot slot;
        Check(slot.Initialize(device, error), "Submission owner initialization");
        Drain drain{device, gate, slot};
        RejectingCache rejected;
        const Capabilities capabilities{.coreForward = true, .layeredLookup = true};
        EnhancedMaterialCoverage coverage;
        coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
        std::shared_ptr<const SceneMaterialPacket> packet;
        auto currentEvaluation = evaluation(1.3f, false);
        const auto prepare = [&](IRenderPipelineCache& cache, const SceneSurfaceEvaluation& values,
                                 const EnhancedMaterialCoverage& policy, const IblEnvironment& env,
                                 std::shared_ptr<const SceneMaterialPacket>& result) {
            const bool coat = material_graph_test::SamePinnedObject(values.instance->generation, layered);
            const auto& layout = layouts[coat ? 1 : 0];
            RHIGraphicsPipelineDesc pipeline;
            pipeline.layout = layout.material.handle;
            pipeline.rtvFormats[0] = description.format;
            pipeline.blendEnable = (policy.flags & EnhancedMaterialCoverage::Blended) != 0;
            pipeline.depthWriteMask = pipeline.blendEnable ? RHIDepthWrite::Zero : RHIDepthWrite::All;
            pipeline.cullMode =
                (policy.flags & EnhancedMaterialCoverage::DoubleSided) ? RHICullMode::None : RHICullMode::Back;
            return slot.Prepare(textures, cache, bindings, baker, values, env, policy, layout, pipeline,
                                RHIShaderBinary::Dxil, capabilities, {}, result, error);
        };
        const auto draw = [&](unsigned target, const SceneMaterialPacket& value) {
            const auto owner = value.pipeline->GetGeneration();
            Check(owner && owner->shader.compile.backend == RHIShaderBinary::Dxil &&
                      !owner->shader.compile.sealedProgramIdentity.empty() &&
                      owner->pipeline.GetHandle() == value.pipeline->GetHandle(),
                  "GPU packet retains accepted common LX graphics generation");
            auto& encoder = device.GetImmediateEncoder();
            Check(slot.Bind(encoder, value, error), "Native packet bind: " + error);
            const std::array<std::uint32_t, 4> constants{value.coverage.flags,
                                                         std::bit_cast<std::uint32_t>(value.coverage.cutoff), 0, 0};
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                      device.UploadConstants(constants.data(), sizeof(constants)));
            const RHITransition before{targets[target], targetStates[target], RHIResourceState::RenderTarget};
            encoder.ResourceBarriers({{&before, 1}});
            const auto rtv = device.CreateRenderTargets(std::span(&targets[target], 1));
            Check(rtv.IsValid(), "Native RTV");
            encoder.BindRenderTargets(rtv);
            const float clear[] = {0, 0, 0, 0};
            encoder.ClearRenderTargets(rtv, clear);
            encoder.SetViewportAndScissor(1, 1);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
            const RHITransition after{targets[target], RHIResourceState::RenderTarget, RHIResourceState::CopySource};
            encoder.ResourceBarriers({{&after, 1}});
            targetStates[target] = RHIResourceState::CopySource;
            encoder.CopyToReadback(readbacks[target], targets[target]);
        };
        const auto publish = [&](std::uint64_t recording, std::uint64_t submittedFence = 0) {
            Check(GetRHISubmissionThread().DrainSubmissions(&device, error),
                  "Native CPU queue submission succeeds without waiting for GPU completion");
            const RHICompletionPoint completion{submittedFence ? submittedFence : device.GetLastSignaledFenceValue()};
            Check(!slot.PublishSubmitted(recording, {completion.value + 1}, error), "Unobserved fence cannot publish");
            const bool published = slot.PublishSubmitted(recording, completion, error);
            Check(published, "Host-confirmed submission publication: " + error);
        };
        const auto pixels = [&](unsigned target, float ior, bool coat, bool blended = false) {
            RHIReadbackImage values;
            Check(device.MapReadback(readbacks[target], values, error), "Native readback map");
            const double ratio = (double(ior) - 1) / (double(ior) + 1);
            const double fresnel = ratio * ratio;
            double ambient = 2 * (base * (1 - fresnel) + fresnel);
            if (coat)
            {
                ambient = ambient * (1 - .35 * .04) + 2 * .35 * .04;
            }
            // The product's regular blend path preserves destination alpha.
            const std::array expected{ambient, double(ior), fresnel, blended ? 0.0 : 1.0};
            for (unsigned c = 0; c < 4; ++c)
            {
                const float actual = values.At(0, 0, c);
                Check(std::isfinite(actual) && std::abs(actual - expected[c]) < 1e-4,
                      "Scene generation/IBL/texture GPU channel " + std::to_string(c) +
                          " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected[c]));
                ++gpuComponents;
            }
        };
        Check(!prepare(pipelines, currentEvaluation, coverage, environment, packet) && !packet,
              "No preparation outside frame");
        Check(SUCCEEDED(
                  device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.fence.GetAddressOf()))),
              "Native queue gate fence");
        Check(SUCCEEDED(device.GetCommandQueue()->Wait(gate.fence.Get(), 1)),
              "Block GPU execution without blocking CPU");
        std::array<std::weak_ptr<const SceneMaterialPacket>, 2> weak;
        std::array<RHIBufferHandle, 2> buffers;
        std::array<std::uint64_t, 2> fences;
        for (unsigned frame = 0; frame < 2; ++frame)
        {
            Check(device.BeginFrame(error), "Overlapping BeginFrame");
            textures.BeginFrame(frame + 1);
            if (frame)
            {
                Check(!slot.Bind(device.GetImmediateEncoder(), *slot.Active(), error),
                      "Reject stale-frame active bindings");
            }
            currentEvaluation = evaluation(frame ? 2.7f : 1.3f, frame != 0, frame + 1);
            Check(prepare(pipelines, currentEvaluation, coverage, environment, packet),
                  "Native Scene prepare: " + error);
            Check(packet->instancePins && packet->instancePinIndex != InstanceFramePins::InvalidIndex &&
                      material_graph_test::SamePinnedObject(
                          packet->instancePins->Borrow(packet->instancePinIndex), currentEvaluation.instance),
                  "Scene packet's borrowed evaluation retains the exact source through its frame pin index");
            Check(packet->queue == SceneCoverage::Opaque && packet->selection.route == Route::Forward,
                  "Opaque queue remains separate from Forward shading");
            Check((frame == 0 && !slot.Active()) || (frame == 1 && slot.Active()->serial != packet->serial),
                  "Preparation does not publish before submission confirmation");
            Check(!slot.PublishSubmitted(device.GetCurrentUploadRecordingId(), {}, error),
                  "No speculative publication");
            weak[frame] = packet;
            buffers[frame] = packet->ibl->Buffer();
            draw(frame, *packet);
            const auto recording = device.GetCurrentUploadRecordingId();
            Check(device.EndFrame(error), "Overlapping native submission");
            Check((frame == 0 && !slot.Active()) || (frame == 1 && slot.Active()->serial != packet->serial),
                  "RHI allocation submission callback alone is not publication");
            publish(recording);
            Check(slot.Active() == packet && material_graph_test::SamePinnedObject(slot.Active()->evaluation.instance, currentEvaluation.instance),
                  "Publish complete owning generation");
            fences[frame] = device.GetLastSignaledFenceValue();
            packet.reset();
        }
        Check(device.GetCompletedFenceValue() < fences[0] && fences[0] < fences[1],
              "Two GPU submissions are genuinely in flight");
        Check(!weak[0].expired() && !weak[1].expired() && device.Resolve(buffers[0]) && device.Resolve(buffers[1]) &&
                  slot.RetainedRecordingCount() == 2,
              "Both owning generations and IBL buffers survive in-flight replacement");
        gate.Release();
        device.WaitForGpu();
        pixels(0, 1.3f, false);
        pixels(1, 2.7f, true);

        Check(device.BeginFrame(error), "Failure/reuse BeginFrame");
        textures.BeginFrame(3);
        Check(weak[0].expired() && !device.Resolve(buffers[0]),
              "Completion callback releases superseded native IBL buffer");
        const auto accepted = slot.Active();
        RHIGraphicsPipelineDesc mismatchedCoverage;
        mismatchedCoverage.layout = layouts[1].material.handle;
        mismatchedCoverage.rtvFormats[0] = description.format;
        mismatchedCoverage.blendEnable = true;
        mismatchedCoverage.depthWriteMask = RHIDepthWrite::Zero;
        Check(!slot.Prepare(textures, pipelines, bindings, baker, currentEvaluation, environment, coverage, layouts[1],
                            mismatchedCoverage, RHIShaderBinary::Dxil, capabilities, {}, packet, error) &&
                  slot.Active() == accepted,
              "Opaque Forward cannot inherit a transparent PSO policy");
        Check(!prepare(rejected, currentEvaluation, coverage, environment, packet) && !packet &&
                  slot.Active() == accepted,
              "PSO creation failure preserves the complete submitted packet");
        auto bad = currentEvaluation;
        bad.points[0].metalIorLevelAo[1] = std::numeric_limits<float>::quiet_NaN();
        Check(!prepare(pipelines, bad, coverage, environment, packet) && slot.Active() == accepted,
              "Bake validation failure preserves the complete submitted packet");
        bad = currentEvaluation;
        bad.sceneEpoch = 0;
        Check(!prepare(pipelines, bad, coverage, environment, packet), "Missing spatial identity is rejected");
        bad = currentEvaluation;
        bad.points[0].viewTier[3] = 0;
        Check(!prepare(pipelines, bad, coverage, environment, packet), "Layered point mismatch is rejected");
        auto wrongEnvironment = environment;
        wrongEnvironment.owner.reset();
        Check(!prepare(pipelines, currentEvaluation, coverage, wrongEnvironment, packet),
              "Missing environment owner is rejected");
        auto wrongCoverage = coverage;
        wrongCoverage.flags |= EnhancedMaterialCoverage::Masked | EnhancedMaterialCoverage::Blended;
        Check(!prepare(pipelines, currentEvaluation, wrongCoverage, environment, packet),
              "Invalid coverage is rejected");
        Check(prepare(pipelines, currentEvaluation, coverage, environment, packet),
              "Rebuild current recording bindings");
        Check(packet->ibl == accepted->ibl && packet->bindings != accepted->bindings &&
                  packet->pipeline->GetHandle() == accepted->pipeline->GetHandle(),
              "Reuse exact IBL and shared PSO with fresh frame bindings");
        const auto ready = packet;
        Check(!prepare(rejected, currentEvaluation, coverage, environment, packet) && packet == ready &&
                  slot.Active() == accepted,
              "Failure keeps both last submitted and current prepared packets");
        device.AbortFrame();
        Check(slot.Active() == accepted, "Aborted prepared replacement is never published");
        packet.reset();

        Check(device.BeginFrame(error), "Aborted bake BeginFrame");
        textures.BeginFrame(4);
        currentEvaluation = evaluation(1.8f, false, 4);
        Check(prepare(pipelines, currentEvaluation, coverage, environment, packet), "New recorded bake before abort");
        const auto abortedBuffer = packet->ibl->Buffer();
        const std::weak_ptr aborted = packet;
        packet.reset();
        Check(device.Resolve(abortedBuffer), "Recorded candidate buffer is owned");
        device.AbortFrame();
        Check(aborted.expired() && !device.Resolve(abortedBuffer) && slot.Active() == accepted,
              "Wholly aborted bake is released and last submitted packet survives");

        Check(device.BeginFrame(error), "Partial submission BeginFrame");
        textures.BeginFrame(5);
        currentEvaluation = evaluation(1.8f, false, 5);
        Check(prepare(pipelines, currentEvaluation, coverage, environment, packet), "Prepare partial submission");
        const auto partialRecording = device.GetCurrentUploadRecordingId();
        gate.fence.Reset();
        Check(SUCCEEDED(
                  device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.fence.GetAddressOf()))),
              "Partial submission queue gate");
        Check(SUCCEEDED(device.GetCommandQueue()->Wait(gate.fence.Get(), 1)), "Block submitted prefix execution");
        Check(device.FlushCommandList(error), "Native successful partial submission");
        publish(partialRecording);
        Check(slot.Active() == packet, "Confirmed partial submission publishes its prepared generation");
        const auto partial = packet;
        Check(device.GetCurrentUploadRecordingId() != partialRecording, "Flush opens a distinct tail recording");
        currentEvaluation = evaluation(2.0f, false, 6);
        Check(prepare(pipelines, currentEvaluation, coverage, environment, packet), "Prepare unsubmitted tail");
        const auto tailBuffer = packet->ibl->Buffer();
        const std::weak_ptr tail = packet;
        packet.reset();
        device.AbortFrame();
        Check(slot.Active() == partial && tail.expired() && !device.Resolve(tailBuffer) &&
                  device.Resolve(partial->ibl->Buffer()) && slot.RetainedRecordingCount() == 1,
              "Abort releases unsubmitted tail while retaining the in-flight submitted prefix");
        gate.Release();
        device.WaitForGpu();
        Check(device.BeginFrame(error), "Collect partial recording");
        textures.BeginFrame(6);
        Check(tail.expired() && !device.Resolve(tailBuffer), "Completed aborted tail owner is reclaimed");
        currentEvaluation = evaluation(1.8f, false, 7);
        auto changedEnvironment = environment;
        changedEnvironment.generation = 2;
        Check(prepare(pipelines, currentEvaluation, coverage, changedEnvironment, packet) &&
                  packet->ibl != partial->ibl,
              "Environment generation forces a new owning bake");
        wrongCoverage = coverage;
        wrongCoverage.flags |= EnhancedMaterialCoverage::Masked;
        Check(prepare(pipelines, currentEvaluation, wrongCoverage, changedEnvironment, packet) &&
                  packet->queue == SceneCoverage::Masked,
              "Masked coverage stays distinct from physical route");
        draw(0, *packet);
        const auto finalRecording = device.GetCurrentUploadRecordingId();
        Check(device.EndFrame(error), "Final native submission");
        publish(finalRecording);
        device.WaitForGpu();
        pixels(0, 1.8f, false);
        Check(device.BeginFrame(error), "Blended queue BeginFrame");
        textures.BeginFrame(7);
        wrongCoverage = coverage;
        wrongCoverage.flags |= EnhancedMaterialCoverage::Blended;
        Check(prepare(pipelines, currentEvaluation, wrongCoverage, changedEnvironment, packet) &&
                  packet->queue == SceneCoverage::Blended && packet->pipeline->GetDesc().blendEnable &&
                  packet->pipeline->GetDesc().depthWriteMask == RHIDepthWrite::Zero,
              "Blended coverage uses its own sort queue and fixed-function state");
        draw(1, *packet);
        const auto blendedRecording = device.GetCurrentUploadRecordingId();
        Check(device.EndFrame(error), "Blended native submission");
        const auto blendedFence = device.GetLastSignaledFenceValue();
        device.WaitForGpu();
        Check(slot.Active() != packet && slot.RetainedRecordingCount() == 1,
              "GPU completion before host confirmation preserves the unpublished candidate");
        publish(blendedRecording, blendedFence);
        Check(slot.Active() == packet, "Late submission confirmation publishes the retained complete packet");
        pixels(1, 1.8f, false, true);
        const auto lastGood = slot.Active();
        Check(device.BeginFrame(error), "Rejected publication BeginFrame");
        textures.BeginFrame(8);
        currentEvaluation = evaluation(2.2f, false, 8);
        Check(prepare(pipelines, currentEvaluation, coverage, changedEnvironment, packet),
              "Candidate for explicit publication rejection");
        const auto rejectedBuffer = packet->ibl->Buffer();
        const std::weak_ptr rejectedOwner = packet;
        gate.fence.Reset();
        Check(SUCCEEDED(
                  device.GetDevice()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.fence.GetAddressOf()))),
              "Rejected publication queue gate");
        Check(SUCCEEDED(device.GetCommandQueue()->Wait(gate.fence.Get(), 1)), "Block rejected candidate execution");
        const auto rejectedRecording = device.GetCurrentUploadRecordingId();
        Check(device.EndFrame(error), "Recorded rejected candidate submission");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error),
              "Confirmed rejected candidate native submission");
        packet.reset();
        slot.RejectSubmitted(rejectedRecording);
        Check(!slot.PublishSubmitted(rejectedRecording, {device.GetLastSignaledFenceValue()}, error) &&
                  slot.Active() == lastGood,
              "A rejected generation cannot be revived by a delayed publication");
        Check(slot.Active() == lastGood && !rejectedOwner.expired() && device.Resolve(rejectedBuffer),
              "Explicit rejection preserves last good and retains in-flight candidate resources");
        gate.Release();
        device.WaitForGpu();
        Check(rejectedOwner.expired() && !device.Resolve(rejectedBuffer) && slot.Active() == lastGood,
              "Rejected publication resources release only on completion");
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "D3D12 GPU validation: " + messages);
        packet.reset();
    }
    bindings.Clear();
    for (unsigned i = 0; i < 2; ++i)
    {
        device.ReleaseReadback(readbacks[i]);
        device.ReleaseTexture(targets[i]);
    }
    textures.Shutdown();
    pipelines.Shutdown();
    roots.Shutdown();
    device.Shutdown();
    std::cout << "LX_MATERIAL_SCENE_PACKET_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " inFlight=2 compiled=10\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Expected repository root");
        Run(std::filesystem::absolute(argv[1]));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_SCENE_PACKET_FAILED " << error.what() << '\n';
        return 1;
    }
}
