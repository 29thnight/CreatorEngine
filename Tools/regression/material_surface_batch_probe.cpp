#include "MaterialGraphSurfaceBatch.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "material_ibl_reference.h"

#include <bit>
#include <iostream>

namespace
{
using namespace material_graph;
using namespace LX;
using namespace MaterialProbe::IblReference;
std::size_t checks{}, gpuComponents{};
double maxError{};
double maxColorError{};
constexpr unsigned kPoints = 37;

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void Near(float actual, double expected, const std::string& message, double tolerance = 1e-4)
{
    const double difference = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
    maxError = std::max(maxError, difference);
    Check(std::isfinite(actual) && difference <= tolerance,
          message + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
    ++gpuComponents;
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
    throw std::runtime_error("Missing pin " + name);
}

VerifiedProduct Product(const std::filesystem::path& root, bool layered)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -200, 0);
    const auto ior = asset.CreateNode("LXParameterFloat", -200, 100);
    const auto level = asset.CreateNode("LXParameterFloat", -200, 200);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{900, "ior", "IOR", PinType::Float, 1.3}, {901, "level", "Level", PinType::Float, .5}};
    Check(asset.graph.SetProperty(ior, "parameter", "900"), "IOR parameter");
    Check(asset.graph.SetProperty(level, "parameter", "901"), "Level parameter");
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"), "Texture GUID");
    Check(asset.graph.SetProperty(image, "interpolation", "Closest"), "Nearest sampler");
    Check(asset.graph.SetProperty(image, "extension", "EXTEND"), "Clamp sampler");
    for (const auto& [name, value] : {std::pair{"Coat Weight", layered ? .4 : 0.0},
                                      {"Coat Roughness", .3},
                                      {"Sheen Weight", layered ? .2 : 0.0},
                                      {"Anisotropic", layered ? .45 : 0.0},
                                      {"Anisotropic Rotation", layered ? .25 : 0.0},
                                      {"Thin Film Thickness", layered ? 420.0 : 0.0}})
    {
        const auto pin = Pin(asset.graph, surface, name, Direction::Input);
        asset.graph.SetSocketValue(pin, value);
        Check(asset.graph.FindPin(pin)->value == LXSocketValue{value}, name);
    }
    const auto connect = [&](Id from, const std::string& fromName, Id to, const std::string& toName) {
        Check(asset.graph
                  .Connect(Pin(asset.graph, from, fromName, Direction::Output),
                           Pin(asset.graph, to, toName, Direction::Input))
                  .has_value(),
              "Fixture link");
    };
    connect(image, "Color", surface, "Base Color");
    connect(image, "Alpha", surface, "Roughness");
    connect(ior, "Value", surface, "IOR");
    connect(level, "Value", surface, "Specular IOR Level");
    connect(surface, "BSDF", output, "Surface");
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!program, "Generate spatial material");
    const auto file =
        root / "Build/Obj/MaterialProductProbe" / (layered ? "surface-layered.slang" : "surface-core.slang");
    std::ofstream(file, std::ios::binary | std::ios::trunc) << BuildSurfaceSource(*program);
    const CompileTarget targets[] = {
        {RHIShaderBinary::Dxil, "LXEvaluateSurface", "cs_6_0"}, {RHIShaderBinary::Dxil, "LXSurfaceVS", "vs_6_0"},
        {RHIShaderBinary::Dxil, "LXSurfacePS", "ps_6_0"},       {RHIShaderBinary::SpirV, "LXEvaluateSurface", "cs_6_0"},
        {RHIShaderBinary::SpirV, "LXSurfaceVS", "vs_6_0"},      {RHIShaderBinary::SpirV, "LXSurfacePS", "ps_6_0"}};
    RHIShaderCompileOptions options;
    options.strictMath = true;
    options.includeDirectories.push_back(root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes");
    VerifiedProduct result;
    const bool verified = VerifySurfaceProduct(*program, file, targets, options, {}, result, diagnostics);
    std::string messages;
    for (const auto& diagnostic : diagnostics)
    {
        messages += diagnostic.message + "\n";
    }
    Check(verified, "Verify spatial host: " + messages);
    const auto key = result.program.semanticKey;
    const auto wrong = root / "Build/Obj/MaterialProductProbe/surface-wrong.slang";
    std::ofstream(wrong, std::ios::binary | std::ios::trunc) << BuildSurfaceSource(*program) << "\n// Changed host\n";
    Check(!VerifySurfaceProduct(*program, wrong, targets, options, {}, result, diagnostics) &&
              result.program.semanticKey == key,
          "Reject different host and retain product");
    std::vector<std::uint8_t> bytes;
    std::string error;
    CookedProgram cooked;
    Check(WriteCookedProgram(result, {}, bytes, error) && ReadCookedProgram(bytes, {}, cooked, error) &&
              cooked.product.targets.size() == 6,
          "CS/VS/PS owning cook round trip");
    return cooked.product;
}

using Texel = std::array<std::uint8_t, 4>;
constexpr std::array<Texel, 8> kTexels{{{64, 128, 192, 0},
                                        {192, 64, 128, 64},
                                        {128, 192, 64, 128},
                                        {255, 128, 32, 255},
                                        {32, 64, 128, 64},
                                        {128, 32, 64, 128},
                                        {64, 128, 32, 255},
                                        {192, 192, 128, 0}}};
constexpr std::array<Texel, 2> kMip{{{96, 160, 224, 96}, {224, 96, 160, 160}}};

own::shared_owner<const Texture> Image()
{
    auto image = TextureImage::Allocate(RHIFormat::RGBA8UnormSrgb, 4, 2, 1, 2, false);
    std::memcpy(image.MutablePixelsAt(*image.Find(0, 0)), kTexels.data(), sizeof(kTexels));
    std::memcpy(image.MutablePixelsAt(*image.Find(1, 0)), kMip.data(), sizeof(kMip));
    return Texture::CreateSharedFromImage("LX.Spatial.Image", std::move(image));
}

own::shared_owner<const Texture> Cube(const Environment& environment)
{
    auto image = TextureImage::Allocate(RHIFormat::RGBA32Float, 1, 1, 6, 1, true);
    for (unsigned face = 0; face < 6; ++face)
    {
        const auto value = Pack4(environment[face], 1);
        std::memcpy(image.MutablePixelsAt(*image.Find(0, face)), value.data(), sizeof(value));
    }
    return Texture::CreateSharedFromImage("LX.Spatial.Environment", std::move(image));
}

std::vector<SurfacePoint> Points()
{
    std::vector<SurfacePoint> points(kPoints);
    for (unsigned i = 0; i < kPoints; ++i)
    {
        auto& point = points[i];
        point.uvLod = {(float(i % 4) + .5f) / 4, (float((i / 4) % 2) + .5f) / 2, 0, i % 7 == 0 ? 1.f : 0.f};
        point.position = {.03f * float(i % 5), -.02f * float(i % 3), 0, 0};
        point.normal = {.1f * float(i % 3), -.07f * float(i % 2), 1, 0};
        point.tangent = {1, .1f * float(i % 4), 0, 0};
        point.bitangent = {0, 1, .1f, 0};
    }
    return points;
}

IblBakePoint ExpectedPoint(const SurfacePoint& input, const SurfaceView& view, float ior, float level, bool layered)
{
    IblBakePoint result;
    const auto& pixel = input.uvLod[3] == 1 ? kMip[unsigned(input.uvLod[0] * 2)]
                                            : kTexels[unsigned(input.uvLod[1] * 2) * 4 + unsigned(input.uvLod[0] * 4)];
    for (unsigned c = 0; c < 3; ++c)
    {
        const double srgb = pixel[c] / 255.0;
        result.baseAlpha[c] = float(srgb <= .04045 ? srgb / 12.92 : std::pow((srgb + .055) / 1.055, 2.4));
    }
    result.normalRoughness = Pack4(Unit(Rgb4(input.normal)), pixel[3] / 255.0);
    result.metalIorLevelAo = {0, ior, level, 1};
    result.tintAnisotropy[3] = layered ? .45f : 0;
    result.coatWeightRoughIorFilmThickness = {layered ? .4f : 0, layered ? .3f : .03f, 1.5f, layered ? 420.f : 0};
    result.coatNormalSheenWeight = Pack4(layered ? Unit(Rgb4(input.normal)) : Vector{0, 0, 1}, layered ? .2 : 0);
    result.tangentRotation =
        Pack4(Projected(layered ? Rgb4(input.tangent) : Vector{1, 0, 0}, Unit(Rgb4(input.normal))), layered ? .25 : 0);
    result.viewTier = Pack4(Unit(Rgb4(view.eye) - Rgb4(input.position)), 1);
    return result;
}

class RejectingCache final : public IRenderPipelineCache
{
  public:
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc&, std::string&) override { return {}; }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc&, std::string& error) override
    {
        error = "Injected surface PSO rejection";
        return {};
    }
    bool InvalidatePipeline(RHIPipelineHandle, RHICompletionPoint) override { return false; }
    std::uint32_t InvalidatePipelines(RHICompletionPoint) override { return 0; }
    std::uint32_t CollectRetiredPipelines(RHICompletionPoint) override { return 0; }
};

struct Drain
{
    DX12DeviceResources& device;
    ~Drain()
    {
        if (device.GetCurrentUploadRecordingId())
        {
            device.AbortFrame();
        }
        device.WaitForGpu();
    }
};

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const std::array products{Product(root, false), Product(root, true)};
    const auto table = LoadSheenTable(root / "Tools/blender/fixtures/principled-layered-5.1.1/sheen-ltc.csv");
    const Environment uniform{{{2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}}};
    const Environment directional{{{8, .1, .2}, {.2, 3, .1}, {.1, .2, 6}, {3, 2, .1}, {.1, 1, 2}, {1, .3, .1}}};
    const auto image = Image();
    const std::array environments{Cube(uniform), Cube(directional)};
    GenerationStore store;
    experiment::AssetId graph;
    Check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graph.value), "Graph GUID");
    std::array<own::shared_owner<const Generation>, 2> generations;
    std::string error;
    for (unsigned i = 0; i < 2; ++i)
    {
        generations[i] = store.Load(
            graph,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[i], WriteMaterialProgramMetadata(products[i].program),
                          BuildBoundSource(products[i].program)};
                return true;
            },
            true, error);
        Check(!!generations[i], "Generation " + error);
    }
    DX12DeviceResources device;
    Check(device.Initialize(kPoints, 1, error), "Native device: " + error);
    Check(device.HasDebugMessageQueue(), "Native validation queue");
    DX12RootSignatureCache roots;
    DX12PSOManager pipelines;
    DX12TextureCache textures;
    Check(roots.Initialize(&device, error), "Root cache");
    Check(pipelines.Initialize(&device, L"", error), "PSO cache");
    Check(textures.Initialize(&device, error), "Texture cache");
    RenderBindingCache bindings;
    std::array<SurfaceEvaluator, 2> evaluators;
    std::array<PassLayout, 2> graphicsLayouts;
    std::array<RHIGraphicsPipelineRequest, 2> graphics;
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(evaluators[i].Initialize(device, roots, pipelines, products[i], RHIShaderBinary::Dxil, {}, error),
              "Native evaluator: " + error);
        const RHIPipelineLayoutParam host[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(1), RHILayout::Srv(2)};
        Check(CreatePassLayout(roots, products[i].layout, host, {}, false, graphicsLayouts[i], error),
              "Graphics layout");
        RHIGraphicsPipelineDesc description;
        description.layout = graphicsLayouts[i].handle;
        description.rtvFormats[0] = RHIFormat::RGBA32Float;
        for (const auto& shader : products[i].shaders)
        {
            if (shader.backend == "dxil" && shader.entryPoint == "LXSurfaceVS")
            {
                description.vsBytecode = shader.bytecode.data();
                description.vsSize = shader.bytecode.size();
            }
            if (shader.backend == "dxil" && shader.entryPoint == "LXSurfacePS")
            {
                description.psBytecode = shader.bytecode.data();
                description.psSize = shader.bytecode.size();
            }
        }
        Check(graphics[i].Create(pipelines, description, error), "Owning graphics PSO");
    }
    RejectingCache rejected;
    const auto layout = evaluators[0].Layout().handle;
    Check(!evaluators[0].Initialize(device, roots, rejected, products[1], RHIShaderBinary::Dxil, {}, error) &&
              evaluators[0].Layout().handle == layout,
          "PSO rejection retains Core evaluator");
    IblBaker baker;
    RHIShaderBlob shader;
    const auto file = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang";
    for (const auto backend : {RHIShaderBinary::SpirV, RHIShaderBinary::Dxil})
    {
        RHIShaderCompiler::ScopedOutput output(backend);
        Check(RHIShaderCompiler::CompileFile(file.string(), "CSMain", "cs_6_0", shader, error),
              "Bake compile: " + error);
    }
    Check(baker.Initialize(device, roots, pipelines, shader, error), "Baker");
    RHITextureDesc targetDescription;
    targetDescription.width = kPoints;
    targetDescription.height = 1;
    targetDescription.format = RHIFormat::RGBA32Float;
    targetDescription.allowRenderTarget = true;
    RHITextureHandle target;
    std::array<RHIReadback, 3> readbacks;
    Check(device.CreateTexture(targetDescription, target, error), "Draw target");
    Check(device.CreateBufferReadback(kPoints * sizeof(IblBakePoint), readbacks[0], error), "Point readback");
    Check(device.CreateBufferReadback(kPoints * sizeof(IblBakeSample), readbacks[1], error), "Bake readback");
    Check(device.CreateReadback(kPoints, 1, RHIFormat::RGBA32Float, 1, readbacks[2], error), "Draw readback");
    std::shared_ptr<const SurfaceBatch> batch;
    std::shared_ptr<const IblBakeResult> baked;
    std::shared_ptr<const SurfaceBatch> lastGoodBatch;
    std::shared_ptr<const IblBakeResult> lastGoodBake;
    own::shared_owner<const Instance> previousInstance;
    std::shared_ptr<const RenderBindings> staleBindings;
    auto points = Points();
    SurfaceView view{{0, 0, 2, 0}, 1, 1, 1};
    auto targetState = RHIResourceState::Common;
    Check(!baker.RecordGpu(device, {}, {}, baked, error) && !baked, "Reject missing GPU points");
    {
        Drain drain{device};
        for (unsigned frame = 0; frame < 8; ++frame)
        {
            const bool layered = frame >= 4;
            const bool invalid = frame >= 6;
            const unsigned tier = layered ? 1 : 0;
            const float ior = frame < 3 ? 1.3f : frame == 7 ? 1e7f : 2.3f;
            const float level = frame < 3 ? .5f : .8f;
            if (frame == 2)
            {
                view.eye = {.7f, .1f, 2, 0};
                ++view.viewRevision;
            }
            if (frame == 5)
            {
                view.eye = {-.8f, .4f, 1.4f, 0};
                ++view.viewRevision;
                ++view.geometryRevision;
                for (auto& point : points)
                {
                    point.position[0] += .15f;
                }
            }
            if (frame == 6)
            {
                view.eye[2] = -2;
                ++view.viewRevision;
            }
            if (frame == 7)
            {
                view.eye[2] = 2;
                ++view.viewRevision;
            }
            own::shared_owner<const Instance> instance;
            if (frame == 1)
            {
                instance = previousInstance;
            }
            else
            {
                Check(BuildInstance(
                          generations[tier], {graph, {{900, double(ior)}, {901, double(level)}}, {}},
                          [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, instance,
                          error),
                      "Typed spatial instance " + error);
            }
            Check(device.BeginFrame(error), "Begin spatial frame");
            textures.BeginFrame(frame);
            std::shared_ptr<const RenderBindings> computeBindings, drawBindings;
            Check(bindings.Prepare(device, textures, instance, evaluators[tier].Layout(), computeBindings, error),
                  "Compute bindings: " + error);
            Check(bindings.Prepare(device, textures, instance, graphicsLayouts[tier], drawBindings, error),
                  "Graphics bindings: " + error);
            auto& encoder = device.GetImmediateEncoder();
            std::vector<RHITransition> transitions;
            if (frame == 0)
            {
                const auto entry = textures.GetOrUpload((image ? &*image.borrow() : nullptr), error);
                transitions.push_back(
                    {entry.handle, RHIResourceState::PixelShaderResource, RHIResourceState::ShaderResource});
            }
            const unsigned envIndex = frame >= 5 ? 1 : 0;
            const IblEnvironment environment{textures.GetOrUpload((environments[envIndex] ? &*environments[envIndex].borrow() : nullptr), error), envIndex + 1,
                                             environments[envIndex]};
            if (frame == 0 || frame == 5)
            {
                transitions.push_back(
                    {environment.cube.handle, RHIResourceState::PixelShaderResource, RHIResourceState::ShaderResource});
            }
            encoder.ResourceBarriers({transitions});
            const auto acceptedBatch = batch;
            if (staleBindings)
            {
                Check(!evaluators[0].Record(device, staleBindings, view, points, batch, error) &&
                          batch == acceptedBatch,
                      "Stale recording cannot replace evaluated batch");
            }
            auto badView = view;
            badView.geometryRevision = 0;
            Check(!evaluators[tier].Record(device, computeBindings, badView, points, batch, error) &&
                      batch == acceptedBatch,
                  "Missing geometry identity preserves batch");
            auto badPoints = points;
            badPoints[0].uvLod[0] = std::numeric_limits<float>::quiet_NaN();
            Check(!evaluators[tier].Record(device, computeBindings, view, badPoints, batch, error) &&
                      batch == acceptedBatch,
                  "Nonfinite spatial input preserves batch");
            badPoints = points;
            badPoints[0].normal = {};
            Check(!evaluators[tier].Record(device, computeBindings, view, badPoints, batch, error) &&
                      batch == acceptedBatch,
                  "Missing geometric normal preserves batch");
            badPoints = points;
            badPoints[0].uvLod[3] = -1;
            Check(!evaluators[tier].Record(device, computeBindings, view, badPoints, batch, error) &&
                      batch == acceptedBatch,
                  "Implicit/negative LOD is rejected");
            badPoints.resize(IblBaker::MaxPoints + 1);
            Check(!evaluators[tier].Record(device, computeBindings, view, badPoints, batch, error) &&
                      batch == acceptedBatch,
                  "Spatial batch budget preserves batch");
            if (frame == 1)
            {
                Check(batch->Matches(*instance, view, points) && baked->MatchesGpu(device, environment, *batch),
                      "Exact input/view/environment reuse");
            }
            else
            {
                if (batch)
                {
                    Check(!batch->Matches(*instance, view, points), "Changed material/view/geometry rejects reuse");
                }
                Check(evaluators[tier].Record(device, computeBindings, view, points, batch, error),
                      "Record GPU surface: " + error);
                Check(baker.RecordGpu(device, environment, batch, baked, error),
                      "GPU -> bake without readback: " + error);
                Check(baked->MatchesGpu(device, environment, *batch), "Owning GPU bake identity");
            }
            auto changedEnvironment = environment;
            ++changedEnvironment.generation;
            Check(!baked->MatchesGpu(device, changedEnvironment, *batch), "Environment generation rejects bake reuse");
            Check(batch->Count() == kPoints && baked->Count() == kPoints, "Exact dispatch tail count");
            for (unsigned i = 0; i < 2; ++i)
            {
                const auto buffer = i == 0 ? batch->Buffer() : baked->Buffer();
                const RHIBufferTransition before{buffer, RHIResourceState::ShaderResource,
                                                 RHIResourceState::CopySource};
                encoder.ResourceBarriers({{}, {&before, 1}});
                encoder.CopyBufferToReadback(readbacks[i], buffer);
                const RHIBufferTransition after{buffer, RHIResourceState::CopySource, RHIResourceState::ShaderResource};
                encoder.ResourceBarriers({{}, {&after, 1}});
            }
            encoder.SetPipeline(RHIBindPoint::Graphics, graphics[tier].GetHandle());
            Check(RenderBindingCache::Bind(device, encoder, RHIBindPoint::Graphics, *drawBindings, error),
                  "Draw bindings");
            struct Constants
            {
                std::array<std::uint32_t, 4> countTier;
                IblVector eye;
            };
            const Constants constants{{kPoints, 1, 0, 0}, view.eye};
            encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0, device.UploadConstants(&constants, sizeof(constants)));
            const auto spatial = device.AllocateUpload({points.size() * sizeof(SurfacePoint), RHIUploadUsage::Raw, 16});
            Check(spatial.IsWritable(), "Reference draw spatial upload");
            std::memcpy(spatial.cpuAddress, points.data(), points.size() * sizeof(SurfacePoint));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, spatial);
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, RHIBufferSlice::Whole(baked->Buffer()));
            encoder.SetRootBuffer(RHIBindPoint::Graphics, 3, RHIBufferSlice::Whole(batch->Buffer()));
            const RHITransition before{target, targetState, RHIResourceState::RenderTarget};
            encoder.ResourceBarriers({{&before, 1}});
            const auto rtv = device.CreateRenderTargets({&target, 1});
            Check(rtv.IsValid(), "RTV");
            encoder.BindRenderTargets(rtv);
            const float clear[]{0, 0, 0, 0};
            encoder.ClearRenderTargets(rtv, clear);
            encoder.SetViewportAndScissor(kPoints, 1);
            encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            encoder.Draw(3, 1);
            const RHITransition after{target, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
            encoder.ResourceBarriers({{&after, 1}});
            encoder.CopyToReadback(readbacks[2], target);
            targetState = RHIResourceState::CopySource;
            Check(device.EndFrame(error), "Submit spatial frame");
            Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Confirm native submission");
            device.WaitForGpu();
            std::array<RHIReadbackImage, 3> mapped;
            for (unsigned i = 0; i < 3; ++i)
            {
                Check(device.MapReadback(readbacks[i], mapped[i], error), "Map completed spatial chain");
            }
            const auto actualPoints = mapped[0].Elements<IblBakePoint>();
            const auto actualSamples = mapped[1].Elements<IblBakeSample>();
            Check(actualPoints && actualSamples && mapped[0].ElementCount<IblBakePoint>() == kPoints &&
                      mapped[1].ElementCount<IblBakeSample>() == kPoints,
                  "Exact readback counts");
            Check(batch->ValidateReadback({actualPoints, kPoints}, error) != invalid,
                  invalid ? "Invalid GPU batch must be diagnosed" : "Accept finite GPU batch: " + error);
            if (invalid)
            {
                Check(error.find("point 0") != std::string::npos, "GPU diagnostic identifies rejected sample");
                Check(lastGoodBatch && lastGoodBake && lastGoodBatch != batch && lastGoodBake != baked &&
                          lastGoodBatch->IsValidated(),
                      "Failed GPU acceptance retains the last accepted surface/bake pair");
            }
            else
            {
                lastGoodBatch = batch;
                lastGoodBake = baked;
            }
            Check(batch->IsValidated() != invalid, "GPU publication acceptance is separate from recording");
            for (unsigned i = 0; i < kPoints; ++i)
            {
                if (invalid)
                {
                    Near(actualPoints[i].viewTier[3], -1, "Rejected GPU point marker", 0);
                    Near(actualSamples[i].baseAverage[3], -1, "Rejected bake marker", 0);
                    for (unsigned c = 0; c < 4; ++c)
                    {
                        Near(mapped[2].At(i, 0, c), 0, "Rejected sample produces no accepted radiance", 0);
                    }
                    continue;
                }
                auto expectedPoint = ExpectedPoint(points[i], view, ior, level, layered);
                const auto expectedValues = std::bit_cast<std::array<float, 44>>(expectedPoint);
                const auto actualValues = std::bit_cast<std::array<float, 44>>(actualPoints[i]);
                for (unsigned c = 0; c < 44; ++c)
                {
                    if (c < 3)
                    {
                        // SRGB hardware decode has its own precision gate;
                        // isolate it from the stricter transport math gate.
                        const double colorError = std::abs(actualValues[c] - expectedValues[c]);
                        maxColorError = std::max(maxColorError, colorError);
                        Check(std::isfinite(actualValues[c]) && colorError < .001, "SRGB source precision");
                        ++gpuComponents;
                        expectedPoint.baseAlpha[c] = actualValues[c];
                        continue;
                    }
                    Near(actualValues[c], expectedValues[c],
                         "Surface point " + std::to_string(i) + " field " + std::to_string(c));
                }
                const auto expectedSample = ExpectedBake(expectedPoint, envIndex == 0 ? uniform : directional, table);
                const auto expectedBake = std::bit_cast<std::array<float, 36>>(expectedSample);
                const auto actualBake = std::bit_cast<std::array<float, 36>>(actualSamples[i]);
                for (unsigned c = 0; c < 36; ++c)
                {
                    Near(actualBake[c], expectedBake[c],
                         "Spatial bake " + std::to_string(i) + " field " + std::to_string(c));
                }
                const auto ambient = Ambient(expectedPoint, expectedSample, table);
                for (unsigned c = 0; c < 3; ++c)
                {
                    Near(mapped[2].At(i, 0, c), ambient[c],
                         "Spatial consumer " + std::to_string(i) + " channel " + std::to_string(c));
                }
                Near(mapped[2].At(i, 0, 3), 1, "Consumer alpha");
            }
            previousInstance = instance;
            if (frame == 0)
            {
                staleBindings = computeBindings;
            }
            std::string messages;
            Check(device.DrainDebugMessages(messages) == 0, "D3D12 GPU validation: " + messages);
        }
        Check(!evaluators[0].Record(device, staleBindings, view, points, batch, error),
              "No evaluation outside a recording");
        Check(!baker.RecordGpu(device, {}, batch, baked, error), "No GPU bake outside a recording");
        std::shared_ptr<const RenderBindings> abortBindings;
        Check(device.BeginFrame(error), "Begin aborted evaluation");
        textures.BeginFrame(8);
        Check(bindings.Prepare(device, textures, previousInstance, evaluators[1].Layout(), abortBindings, error),
              "Aborted evaluation bindings");
        std::shared_ptr<const SurfaceBatch> aborted;
        Check(evaluators[1].Record(device, abortBindings, view, points, aborted, error), "Record unsubmitted batch");
        device.AbortFrame();
        Check(device.BeginFrame(error), "Begin after evaluation abort");
        textures.BeginFrame(9);
        const IblEnvironment environment{textures.GetOrUpload((environments[1] ? &*environments[1].borrow() : nullptr), error), 2, environments[1]};
        auto acceptedBake = baked;
        Check(!baker.RecordGpu(device, environment, aborted, baked, error) && baked == acceptedBake,
              "Cancelled/unvalidated GPU batch cannot cross recordings");
        acceptedBake.reset();
        aborted.reset();
        device.AbortFrame();
        abortBindings.reset();
        lastGoodBatch.reset();
        lastGoodBake.reset();
        const auto surfaceBuffer = batch->Buffer();
        std::weak_ptr<const SurfaceBatch> weak = batch;
        batch.reset();
        Check(!weak.expired() && device.Resolve(surfaceBuffer), "Bake owner retains GPU source points");
        const auto bakeBuffer = baked->Buffer();
        baked.reset();
        Check(weak.expired() && !device.Resolve(surfaceBuffer) && !device.Resolve(bakeBuffer),
              "Post-completion release reclaims both GPU buffer registrations");
    }
    staleBindings.reset();
    bindings.Clear();
    for (auto& readback : readbacks)
    {
        device.ReleaseReadback(readback);
    }
    device.ReleaseTexture(target);
    textures.Shutdown();
    pipelines.Shutdown();
    roots.Shutdown();
    device.Shutdown();
    std::cout << "LX_MATERIAL_SURFACE_BATCH_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " points=" << kPoints << " frames=8 compiled=14 maxNormalizedError=" << maxError
              << " maxSrgbError=" << maxColorError << '\n';
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
        std::cerr << "LX_MATERIAL_SURFACE_BATCH_FAILED " << error.what() << '\n';
        return 1;
    }
}
