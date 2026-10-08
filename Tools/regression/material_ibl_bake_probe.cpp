#include "MaterialGraphIblBake.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/RHIShaderCompiler.h"
#include "material_ibl_reference.h"

#include <bit>
#include <iostream>

namespace
{
using namespace material_graph;
using namespace MaterialProbe::Reference;
std::size_t checks{}, gpuComponents{};
double maxError{};

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

void Near(float actual, double expected, const std::string& label, double tolerance = 1e-4)
{
    const double error = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
    maxError = std::max(maxError, error);
    Check(std::isfinite(actual) && error <= tolerance,
          label + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
    ++gpuComponents;
}

using namespace MaterialProbe::IblReference;

own::shared_owner<const Texture> Cube(const Environment& environment)
{
    auto image = TextureImage::Allocate(RHIFormat::RGBA32Float, 4, 4, 6, 1, true);
    for (unsigned face = 0; face < 6; ++face)
    {
        auto* pixels = image.MutablePixelsAt(*image.Find(0, face));
        const auto value = Pack4(environment[face], 1);
        for (unsigned i = 0; i < 16; ++i)
        {
            std::memcpy(pixels + i * sizeof(value), value.data(), sizeof(value));
        }
    }
    return Texture::CreateSharedFromImage("LX.IblBake.Environment", std::move(image));
}

std::vector<IblBakePoint> Points()
{
    std::vector<IblBakePoint> points;
    for (const auto& fixture : Fixtures())
    {
        // Nonfinite input is rejected at the product boundary.
        if (fixture.name == "nan_tangent_fallback")
        {
            continue;
        }
        for (unsigned angle = 0; angle < 4; ++angle)
        {
            IblBakePoint point =
                std::bit_cast<IblBakePoint>(fixture.input.options.x != 0 ? ProbeInput{} : fixture.input);
            const float cosine = float(kCosines[angle]);
            const float x = std::sqrt(1 - cosine * cosine);
            point.viewTier = {angle % 2 ? 0 : x, angle % 2 ? x : 0, cosine, 1};
            points.push_back(point);
        }
    }
    for (float roughness : {0.0f, .2f, .65f, 1.0f})
    {
        for (float ior : {.7f, 1.0f, 1.5f, 2.4f})
        {
            IblBakePoint point;
            point.normalRoughness[3] = roughness;
            point.metalIorLevelAo[1] = ior;
            point.viewTier = {.6f, 0, .8f, 0};
            points.push_back(point);
        }
    }
    return points;
}

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const auto shaderRoot = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader";
    const auto table = LoadSheenTable(root / "Tools/blender/fixtures/principled-layered-5.1.1/sheen-ltc.csv");
    RHIShaderCompileOptions options;
    options.strictMath = true;
    options.includeDirectories.push_back(shaderRoot / "Includes");
    RHIShaderBlob bakeShader, vertexShader, pixelShader;
    std::string error;
    for (const auto format : {RHIShaderBinary::Dxil, RHIShaderBinary::SpirV})
    {
        RHIShaderCompiler::ScopedOutput output(format);
        RHIShaderBlob compiled;
        Check(RHIShaderCompiler::CompileFile((shaderRoot / "PrincipledIblBake.slang").string(), "CSMain", "cs_6_0",
                                             compiled, error, options),
              "Compile bake: " + error);
        if (format == RHIShaderBinary::Dxil)
        {
            bakeShader = compiled;
        }
        for (const auto& entry : {std::pair{"VSMain", "vs_6_0"}, std::pair{"PSMain", "ps_6_0"}})
        {
            Check(RHIShaderCompiler::CompileFile((root / "Tools/regression/material_ibl_consume.slang").string(),
                                                 entry.first, entry.second, compiled, error, options),
                  "Compile fixed-work bake consumer: " + error);
            if (format == RHIShaderBinary::Dxil)
            {
                (entry.first == std::string_view("VSMain") ? vertexShader : pixelShader) = compiled;
            }
        }
    }
    DX12DeviceResources device;
    Check(device.Initialize(256, 1, error), "Actual headless device: " + error);
    Check(device.HasDebugMessageQueue(), "GPU validation installed");
    DX12RootSignatureCache roots;
    DX12PSOManager pipelines;
    DX12TextureCache textures;
    Check(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error) &&
              textures.Initialize(&device, error),
          "Actual RHI caches");
    IblBaker baker;
    Check(baker.Initialize(device, roots, pipelines, bakeShader, error), "Actual bake pipeline: " + error);
    Check(!baker.Initialize(device, roots, pipelines, {}, error), "Failed initialization retains pipeline");
    auto points = Points();
    Check(points.size() == 152, "Pinned evaluated-point fixture count");
    const Environment constant{Vector{2, 3, 5}, Vector{2, 3, 5}, Vector{2, 3, 5},
                               Vector{2, 3, 5}, Vector{2, 3, 5}, Vector{2, 3, 5}};
    const Environment directional{Vector{12, .5, .25}, Vector{.1, 2, 1},     Vector{1, 8, .5},
                                  Vector{.2, .3, 4},   Vector{.25, .5, 1.5}, Vector{.1, .1, .1}};
    std::shared_ptr<const IblBakeResult> active;
    Check(!baker.Record(device, {}, points, active, error) && !active, "Reject bake outside recording");
    RHIReadback readback;
    Check(device.CreateBufferReadback(points.size() * sizeof(IblBakeSample), readback, error),
          "Actual buffer readback");
    const RHIPipelineLayoutParam consumeParameters[] = {RHILayout::Srv(0), RHILayout::Srv(1)};
    const auto consumeLayout = roots.GetOrCreate({consumeParameters}, error);
    Check(consumeLayout.IsValid(), "Native lookup consumer layout");
    RHIGraphicsPipelineDesc consumeDescription;
    consumeDescription.layout = consumeLayout;
    consumeDescription.vsBytecode = vertexShader.Data();
    consumeDescription.vsSize = vertexShader.Size();
    consumeDescription.psBytecode = pixelShader.Data();
    consumeDescription.psSize = pixelShader.Size();
    consumeDescription.depthEnable = false;
    consumeDescription.cullMode = RHICullMode::None;
    consumeDescription.numRenderTargets = 1;
    consumeDescription.rtvFormats[0] = RHIFormat::RGBA32Float;
    const auto consumePipeline = pipelines.GetOrCreate(consumeDescription, error);
    Check(consumePipeline.IsValid(), "Native lookup consumer PSO: " + error);
    RHITextureDesc targetDescription;
    targetDescription.width = static_cast<std::uint32_t>(points.size());
    targetDescription.height = 1;
    targetDescription.format = RHIFormat::RGBA32Float;
    targetDescription.allowRenderTarget = true;
    targetDescription.debugName = L"LX.IblBake.Consumer";
    RHITextureHandle target;
    RHIReadback pixelReadback;
    Check(device.CreateTexture(targetDescription, target, error) &&
              device.CreateReadback(targetDescription.width, 1, targetDescription.format, 1, pixelReadback, error),
          "Native consumption render target");
    RHIResourceState targetState = RHIResourceState::Common;
    for (unsigned frame = 0; frame < 3; ++frame)
    {
        if (frame == 2)
        {
            points.front().metalIorLevelAo[1] = 2.4f;
            points.front().normalRoughness[3] = .2f;
            points.front().viewTier = {.6f, 0, .8f, 1};
        }
        const auto& environmentValues = frame == 0 ? constant : directional;
        const auto texture = Cube(environmentValues);
        Check(!!texture, "Owning environment pixels");
        Check(device.BeginFrame(error), "Begin recording: " + error);
        textures.BeginFrame(frame);
        auto& encoder = device.GetImmediateEncoder();
        IblEnvironment environment{textures.GetOrUpload((texture ? &*texture.borrow() : nullptr), error), frame + 1, texture};
        const RHITransition source{environment.cube.handle, RHIResourceState::PixelShaderResource,
                                   RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{&source, 1}});
        Check(baker.Record(device, environment, points, active, error), "Record bake: " + error);
        const auto accepted = active;
        Check(active->Matches(device, environment, points), "Exact evaluated key reuse");
        auto changed = points;
        changed.front().tangentRotation[3] += .25f;
        Check(!active->Matches(device, environment, changed), "Tangent change invalidates");
        changed = points;
        changed.front().metalIorLevelAo[1] += .1f;
        Check(!active->Matches(device, environment, changed), "IOR override invalidates");
        changed = points;
        changed.front().viewTier[0] += .1f;
        Check(!active->Matches(device, environment, changed), "View azimuth invalidates");
        auto staleEnvironment = environment;
        ++staleEnvironment.generation;
        Check(!active->Matches(device, staleEnvironment, points), "Environment generation invalidates");
        Check(!baker.Record(device, staleEnvironment, {}, active, error) && active == accepted,
              "Empty batch retains accepted output");
        changed = points;
        changed.front().viewTier[0] = std::numeric_limits<float>::quiet_NaN();
        Check(!baker.Record(device, environment, changed, active, error) && active == accepted,
              "Nonfinite point retains accepted output");
        changed = points;
        changed.front().viewTier[3] = 0;
        changed.front().tintAnisotropy[3] = .8f;
        Check(!baker.Record(device, environment, changed, active, error) && active == accepted,
              "Core cannot silently discard anisotropy");
        for (const auto badView : {IblVector{0, 0, 0, 1}, IblVector{0, 0, -1, 1}, IblVector{0, 0, 1, 2}})
        {
            changed = points;
            changed.front().viewTier = badView;
            Check(!baker.Record(device, environment, changed, active, error) && active == accepted,
                  "Invalid view/tier retains accepted output");
        }
        changed = points;
        changed.front().coatTintFilmIor[0] = 1e30f;
        Check(!baker.Record(device, environment, changed, active, error) && active == accepted,
              "Unbounded bake inputs retain accepted output");
        const std::vector<IblBakePoint> tooMany(IblBaker::MaxPoints + 1);
        Check(!baker.Record(device, environment, tooMany, active, error) && active == accepted,
              "Bake batch budget retains accepted output");
        for (unsigned invalid = 0; invalid < 5; ++invalid)
        {
            auto badEnvironment = environment;
            switch (invalid)
            {
            case 0:
                badEnvironment.owner.reset();
                break;
            case 1:
                badEnvironment.generation = 0;
                break;
            case 2:
                badEnvironment.cube.isCube = false;
                break;
            case 3:
                badEnvironment.cube.arraySize = 1;
                break;
            case 4:
                badEnvironment.cube.format = RHIFormat::RGBA8UnormSrgb;
                break;
            }
            Check(!baker.Record(device, badEnvironment, points, active, error) && active == accepted,
                  "Invalid environment retains accepted output");
        }
        const RHIBufferTransition copy{active->Buffer(), RHIResourceState::ShaderResource,
                                       RHIResourceState::CopySource};
        encoder.ResourceBarriers({{}, {&copy, 1}});
        encoder.CopyBufferToReadback(readback, active->Buffer());
        const RHIBufferTransition restore{active->Buffer(), RHIResourceState::CopySource,
                                          RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{}, {&restore, 1}});
        const auto consumeInputs =
            device.AllocateUpload({points.size() * sizeof(IblBakePoint), RHIUploadUsage::Raw, 16});
        Check(consumeInputs.IsWritable(), "Consumer point upload");
        std::memcpy(consumeInputs.cpuAddress, points.data(), points.size() * sizeof(IblBakePoint));
        encoder.SetPipeline(RHIBindPoint::Graphics, consumePipeline);
        encoder.SetRootBuffer(RHIBindPoint::Graphics, 0, consumeInputs);
        encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(active->Buffer()));
        const RHITransition toTarget{target, targetState, RHIResourceState::RenderTarget};
        encoder.ResourceBarriers({{&toTarget, 1}});
        const auto renderTargets = device.CreateRenderTargets(std::span(&target, 1));
        Check(renderTargets.IsValid(), "Actual consumer RTV");
        encoder.BindRenderTargets(renderTargets);
        encoder.SetViewportAndScissor(targetDescription.width, 1);
        encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
        encoder.Draw(3, 1);
        const RHITransition toReadback{target, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
        encoder.ResourceBarriers({{&toReadback, 1}});
        targetState = RHIResourceState::CopySource;
        encoder.CopyToReadback(pixelReadback, target);
        Check(device.EndFrame(error), "Submit bake: " + error);
        device.WaitForGpu();
        RHIReadbackImage image;
        Check(device.MapReadback(readback, image, error), "GPU bake readback");
        const auto output = image.Elements<IblBakeSample>();
        Check(image.ElementCount<IblBakeSample>() == points.size() && output, "Exact GPU sample count");
        RHIReadbackImage pixels;
        Check(device.MapReadback(pixelReadback, pixels, error), "Actual draw readback");
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const auto expected = ExpectedBake(points[i], environmentValues, table);
            const auto actualValues = std::bit_cast<std::array<float, 36>>(output[i]);
            const auto expectedValues = std::bit_cast<std::array<float, 36>>(expected);
            for (unsigned c = 0; c < 36; ++c)
            {
                const bool baseFilter = c >= 20 && c < 23;
                const bool coatFilter = c >= 24 && c < 27;
                if (baseFilter || coatFilter)
                {
                    const unsigned channel = c % 4;
                    const float expectedWeight =
                        (coatFilter ? expected.coatSingleAlbedo : expected.baseSingleAlbedo)[channel];
                    const float actualWeight =
                        (coatFilter ? output[i].coatSingleAlbedo : output[i].baseSingleAlbedo)[channel];
                    if (expectedWeight < 1e-4f)
                    {
                        // Near-zero Fresnel makes the normalized RGB ratio
                        // ill-conditioned. Test the physical reflected energy,
                        // plus the radiance bound; never skip its contribution.
                        Check(std::isfinite(actualValues[c]) && actualValues[c] >= 0 && actualValues[c] <= 12.01f,
                              "Weak reflection convolution remains bounded");
                        Near(actualValues[c] * actualWeight, expectedValues[c] * expectedWeight,
                             "Weak reflection energy", 1e-6);
                        continue;
                    }
                }
                Near(actualValues[c], expectedValues[c],
                     "bake frame=" + std::to_string(frame) + " point=" + std::to_string(i) +
                         " component=" + std::to_string(c),
                     1e-4);
            }
            const auto ambient = Ambient(points[i], expected, table);
            for (unsigned c = 0; c < 3; ++c)
            {
                Near(pixels.At(static_cast<std::uint32_t>(i), 0, c), ambient[c],
                     "Native lookup pixel=" + std::to_string(i) + " channel=" + std::to_string(c), 1e-4);
            }
            Near(pixels.At(static_cast<std::uint32_t>(i), 0, 3), 1, "Actual draw alpha");
        }
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "D3D12 validation: " + messages);
    }
    // Post-completion resource destruction is observable in the native table.
    const auto released = active->Buffer();
    active.reset();
    Check(device.Resolve(released) == nullptr, "Neutral buffer release reclaims native registration after completion");
    device.ReleaseReadback(readback);
    device.ReleaseReadback(pixelReadback);
    device.ReleaseTexture(target);
    textures.Shutdown();
    pipelines.Shutdown();
    roots.Shutdown();
    device.Shutdown();
    std::cout << "LX_MATERIAL_IBL_BAKE_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " points=" << points.size() << " frames=3 compiled=6 maxNormalizedError=" << maxError << '\n';
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
        std::cerr << "LX_MATERIAL_IBL_BAKE_FAILED " << error.what() << '\n';
        return 1;
    }
}
