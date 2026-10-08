#include "material_owner_checks.h"
#include "MaterialGraphRasterSurface.h"
#include "Render/Passes/Geometry/EnhancedShadowPass.h"
#include "Render/Graph/ShadowCasterBounds.h"
#include "support/MaterialGraphScenePacket.h"
#include "MaterialGraphSceneInput.h"
#include "MaterialGraphSceneHost.h"
#include "MaterialGraphSceneCompiler.h"
#include "Experiment/Cooked/CookedAssetCatalog.h"
#include "Experiment/Cooked/PakAudioClipByteSource.h"
#include "Render/Passes/Geometry/EnhancedDeferredPass.h"
#include "Render/Passes/Geometry/EnhancedForwardPass.h"
#include "Render/Scene/ExperimentMaterialSealing.h"
#include "Render/Graph/EnhancedMaterialSealHash.h"
#include "Render/Passes/Geometry/EnhancedDecalPass.h"
#include "Render/Passes/Lighting/EnhancedSSGIPass.h"
#include "Render/Passes/Lighting/EnhancedVolumetricFogPass.h"
#include "RHI/DX12/EnhancedIBLGenerator.h"
#include <set>
#include "RHI/DX12/DX12MeshCache.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12CommandListPool.h"
#include "RHI/DX12/DX12Encoder.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "material_ibl_reference.h"
#ifdef LX_PROBE_VULKAN
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#include "RHI/Vulkan/VulkanCommandBufferPool.h"
#include "RHI/Vulkan/VulkanLoader.h"
#endif

#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <thread>
#ifdef _DEBUG
#include <crtdbg.h>
#include <DbgHelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace
{
#ifdef LX_PROBE_VULKAN
class ProbeDevice : public VulkanDeviceResources
{
  public:
    bool Initialize(unsigned width, unsigned height, std::string& error)
    {
        return VulkanDeviceResources::Initialize(width, height, true, error);
    }
};
using ProbeRoots = VulkanPipelineCache;
using ProbePipelines = VulkanPipelineCache;
using ProbeTextures = VulkanTextureCache;
using ProbeMeshes = VulkanMeshCache;
using ProbePool = VulkanCommandBufferPool;
#else
using ProbeDevice = DX12DeviceResources;
using ProbeRoots = DX12RootSignatureCache;
using ProbePipelines = DX12PSOManager;
using ProbeTextures = DX12TextureCache;
using ProbeMeshes = DX12MeshCache;
using ProbePool = DX12CommandListPool;
#endif

std::uint64_t NativePsoWorkers(const ProbePipelines& pipelines)
{
#ifdef LX_PROBE_VULKAN
    return pipelines.GetStats().asyncWorkerExecutions;
#else
    return pipelines.GetStats().m_asyncWorkerExecutions;
#endif
}

using namespace material_graph;
using namespace LX;
using namespace MaterialProbe::IblReference;
std::size_t checks{}, gpuComponents{};
#ifdef _DEBUG
int __cdecl ReportAssertion(int, char* message, int*)
{
    std::cerr << message << '\n';
    const auto process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    if (SymInitialize(process, nullptr, TRUE))
    {
        void* addresses[32];
        const auto count = CaptureStackBackTrace(0, 32, addresses, nullptr);
        for (unsigned i = 0; i < count; ++i)
        {
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement{};
            const auto address = reinterpret_cast<DWORD64>(addresses[i]);
            if (SymFromAddr(process, address, &displacement, symbol))
            {
                std::cerr << symbol->Name;
                IMAGEHLP_LINE64 line{sizeof(line)};
                DWORD offset{};
                if (SymGetLineFromAddr64(process, address, &offset, &line))
                    std::cerr << " " << line.FileName << ':' << line.LineNumber;
                std::cerr << '\n';
            }
        }
        SymCleanup(process);
    }
    return FALSE;
}
#endif
double maxError{}, maxColorError{};
double maxLodError{};

constexpr unsigned kSamples = 41;
void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
    {
        std::cerr << "PROBE_CHECK_FAIL " << message << std::endl;
        throw std::runtime_error(message);
    }
}
void ShutdownNative(ProbeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines, ProbeTextures& textures,
                    ProbePool& pool)
{
    (void)roots;
#ifdef LX_PROBE_VULKAN
    Check(pool.GetEncoderUnimplementedCount() == 0 && device.GetEncoderUnimplementedCount() == 0,
          "Native Vulkan encoder has no unsupported operation");
    device.WaitForGpu();
    device.SetPipelineCache(nullptr);
#endif
    textures.Shutdown();
    pipelines.Shutdown();
#ifndef LX_PROBE_VULKAN
    roots.Shutdown();
#endif
    pool.Shutdown();
    device.Shutdown();
#ifdef LX_PROBE_VULKAN
    std::string validation;
    Check(device.DrainDebugMessages(validation) == 0, "Vulkan validation after shutdown " + validation);
    std::cout << "LX_MATERIAL_VULKAN_SCENE_VALIDATION_OK validation=0 encoderDrops=0\n";
#endif
}

void Near(float actual, double expected, const std::string& message, double tolerance = 1e-4)
{
    const double difference = std::abs(actual - expected) / std::max(1.0, std::abs(expected));
    if (tolerance >= 1e-3)
    {
        maxColorError = std::max(maxColorError, difference);
    }
    else
    {
        maxError = std::max(maxError, difference);
    }
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

VerifiedProduct Product(const std::filesystem::path& root, bool layered, bool footprints = false)
{
    LXMaterialAsset asset;
    const auto surface = asset.CreateNode("ShaderNodeBsdfPrincipled", 0, 0);
    const auto image = asset.CreateNode("ShaderNodeTexImage", -200, 0);
    const auto ior = asset.CreateNode("LXParameterFloat", -200, 100);
    const auto level = asset.CreateNode("LXParameterFloat", -200, 200);
    const auto alpha = asset.CreateNode("LXParameterFloat", -200, 300);
    const auto output = asset.CreateNode("ShaderNodeOutputMaterial", 400, 0);
    asset.activeOutput = output;
    asset.blackboard = {{900, "ior", "IOR", PinType::Float, 1.3},
                        {901, "level", "Level", PinType::Float, .5},
                        {902, "alpha", "Alpha", PinType::Float, 1.0}};
    Check(asset.graph.SetProperty(ior, "parameter", "900"), "IOR parameter");
    Check(asset.graph.SetProperty(level, "parameter", "901"), "Level parameter");
    Check(asset.graph.SetProperty(alpha, "parameter", "902"), "Alpha parameter");
    Check(asset.graph.SetProperty(image, "image", "22222222-2222-4222-8222-222222222222"), "Texture GUID");
    asset.graph.SetProperty(image, "interpolation", footprints ? "Linear" : "Closest");
    asset.graph.SetProperty(image, "extension", footprints ? "REPEAT" : "EXTEND");
    Check(asset.graph.FindNode(image)->properties.at("interpolation") == (footprints ? "Linear" : "Closest"),
          "Image sampler");
    Check(asset.graph.FindNode(image)->properties.at("extension") == (footprints ? "REPEAT" : "EXTEND"),
          "Image address mode");
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
    if (footprints)
    {
        const auto second = asset.CreateNode("LXTextureSample", -400, 0);
        const auto constant = asset.CreateNode("ShaderNodeTexImage", -400, 100);
        const auto vector = asset.CreateNode("LXParameterVector", -600, 100);
        asset.blackboard.push_back({903, "uv", "Constant UV", PinType::Vector, std::array<double, 3>{.23, .37, 0}});
        Check(asset.graph.SetProperty(vector, "parameter", "903"), "Independent Vector input");
        Check(asset.graph.SetSocketValue(Pin(asset.graph, second, "Texture", Direction::Input),
                                         std::string("44444444-4444-4444-8444-444444444444")) &&
                  asset.graph.SetSocketValue(Pin(asset.graph, second, "Sampler", Direction::Input),
                                             std::string("linear-clamp")) &&
                  asset.graph.FindNode(second)->properties.at("colorSpace") == "data",
              "Independent data image");
        Check(asset.graph.SetProperty(constant, "image", "22222222-2222-4222-8222-222222222222") &&
                  asset.graph.SetProperty(constant, "interpolation", "Closest") &&
                  asset.graph.SetProperty(constant, "extension", "EXTEND"),
              "Shared image with another Vector and sampler");
        connect(second, "Alpha", surface, "Roughness");
        connect(vector, "Value", constant, "Vector");
        connect(constant, "Alpha", surface, "Metallic");
    }
    else
    {
        connect(image, "Alpha", surface, "Roughness");
    }
    connect(ior, "Value", surface, "IOR");
    connect(level, "Value", surface, "Specular IOR Level");
    connect(alpha, "Value", surface, "Alpha");
    connect(surface, "BSDF", output, "Surface");
    if (!footprints)
    {
        const auto fixture = root / "Build/Obj/MaterialProductProbe" /
                             (layered ? "scene-cook-layered.shadergraph" : "scene-cook-core.shadergraph");
        std::string error;
        Check(asset.Save(fixture, &error), "Export automatic Scene cook fixture " + error);
        std::ofstream(fixture.string() + ".meta", std::ios::binary | std::ios::trunc)
            << "guid: " << (layered ? "33333333-3333-4333-8333-333333333333" : "11111111-1111-4111-8111-111111111111")
            << '\n';
    }
    std::vector<LXMaterialDiagnostic> diagnostics;
    const auto program = GenerateMaterialSlang(asset, &diagnostics);
    Check(!!program, "Generate spatial material");
    const auto file = root / "Build/Obj/MaterialProductProbe" /
                      (std::string(footprints ? "footprint-" : "raster-") + (layered ? "layered.slang" : "core.slang"));
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
    if (footprints)
        Check(result.layout.textures.size() == 2 && result.layout.samplers.size() == 3 &&
                  result.program.textureSamples == 3,
              "Distinct images share one resource while different Vector/sampler operations remain separate");
    const auto key = result.program.semanticKey;
    const auto wrong = root / "Build/Obj/MaterialProductProbe/raster-wrong.slang";
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

struct FootprintExtent
{
    unsigned width, height, levels;
};

FootprintExtent ImageExtent(bool second, bool resized = false)
{
    return !second ? FootprintExtent{32, 16, 6} : resized ? FootprintExtent{16, 8, 5} : FootprintExtent{8, 64, 7};
}

Texel FootprintTexel(bool second, unsigned mip, unsigned x, unsigned y, unsigned width, unsigned height)
{
    const auto u = [&](unsigned amount) { return x * amount / std::max(1u, width - 1); };
    const auto v = [&](unsigned amount) { return y * amount / std::max(1u, height - 1); };
    if (second)
        return {64, 32, 128, std::uint8_t(100 + 12 * mip + u(23) + v(27))};
    return {std::uint8_t(50 + 20 * mip + u(71)), std::uint8_t(83 + 18 * mip + v(52)),
            std::uint8_t(135 + 8 * mip + u(30)), std::uint8_t(55 + 8 * mip + u(32) + v(20))};
}

own::shared_owner<const Texture> FootprintImage(bool second, bool resized = false)
{
    const auto extent = ImageExtent(second, resized);
    auto image = TextureImage::Allocate(second ? RHIFormat::RGBA8Unorm : RHIFormat::RGBA8UnormSrgb, extent.width,
                                        extent.height, 1, extent.levels, false);
    for (unsigned mip = 0; mip < extent.levels; ++mip)
    {
        const unsigned width = std::max(1u, extent.width >> mip), height = std::max(1u, extent.height >> mip);
        auto* pixels = reinterpret_cast<Texel*>(image.MutablePixelsAt(*image.Find(mip, 0)));
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x)
                pixels[y * width + x] = FootprintTexel(second, mip, x, y, width, height);
    }
    return Texture::CreateSharedFromImage(second ? "LX.Footprint.Data" : "LX.Footprint.SRGB", std::move(image));
}

double ImageLod(const FootprintExtent& extent, unsigned width, unsigned height, double scale)
{
    const double footprint = std::max(.9 * scale * 2 / width * extent.width, .65 * scale * 2 / height * extent.height);
    return std::clamp(std::log2(footprint), 0.0, double(extent.levels - 1));
}

std::array<float, 4> ReferenceImage(bool second, bool resized, double u, double v, double lod, bool nearest = false)
{
    const auto extent = ImageExtent(second, resized);
    const auto sample = [&](unsigned mip) {
        const int width = std::max(1u, extent.width >> mip), height = std::max(1u, extent.height >> mip);
        const auto fetch = [&](int x, int y) {
            if (!second && !nearest)
            {
                x = (x % width + width) % width;
                y = (y % height + height) % height;
            }
            else
            {
                x = std::clamp(x, 0, width - 1);
                y = std::clamp(y, 0, height - 1);
            }
            const auto texel = FootprintTexel(second, mip, x, y, width, height);
            std::array<double, 4> result;
            for (unsigned c = 0; c < 4; ++c)
            {
                const double value = texel[c] / 255.0;
                result[c] =
                    !second && c < 3 ? value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4) : value;
            }
            return result;
        };
        if (nearest)
            return fetch(int(std::floor(u * width)), int(std::floor(v * height)));
        const double x = u * width - .5, y = v * height - .5;
        const int left = int(std::floor(x)), top = int(std::floor(y));
        const double dx = x - left, dy = y - top;
        const auto a = fetch(left, top), b = fetch(left + 1, top), c = fetch(left, top + 1),
                   d = fetch(left + 1, top + 1);
        std::array<double, 4> result;
        for (unsigned component = 0; component < 4; ++component)
            result[component] =
                std::lerp(std::lerp(a[component], b[component], dx), std::lerp(c[component], d[component], dx), dy);
        return result;
    };
    lod = std::clamp(lod, 0.0, double(extent.levels - 1));
    const auto first = unsigned(std::floor(lod));
    const auto a = sample(first), b = sample(std::min(first + 1, extent.levels - 1));
    std::array<float, 4> result;
    for (unsigned c = 0; c < 4; ++c)
        result[c] = float(std::lerp(a[c], b[c], lod - first));
    return result;
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

IblBakePoint ExpectedPoint(const SurfacePoint& input, const SurfaceView& view, float ior, float level, bool layered)
{
    IblBakePoint result;
    const auto& pixel = input.uvLod[3] >= .5f
                            ? kMip[std::min(1u, unsigned(std::clamp(input.uvLod[0], 0.f, 1.f) * 2))]
                            : kTexels[std::min(1u, unsigned(std::clamp(input.uvLod[1], 0.f, 1.f) * 2)) * 4 +
                                      std::min(3u, unsigned(std::clamp(input.uvLod[0], 0.f, 1.f) * 4))];
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

struct Geometry
{
    std::vector<std::byte> vertices;
    std::array<std::uint32_t, 6> indices{0, 1, 2, 3, 4, 5};
    std::array<SurfacePoint, 6> points;
    EnhancedDrawItem draw;
};

Geometry MakeGeometry(bool nearClip, bool reverse)
{
    Geometry geometry;
    auto& view = geometry.draw.modelMeshView;
    Check(Uuid::TryParse("11111111-1111-8111-8111-111111111111", view.handle.modelId), "Model identity");
    Check(Uuid::TryParse("22222222-2222-8222-8222-222222222222", view.handle.meshId), "Mesh identity");
    view.handle.generation = 1;
    view.vertexAttributeMask = assets::kModelVertexMasks.front();
    view.vertexStride = assets::StrideOf(view.vertexAttributeMask);
    view.vertexLayoutHash = assets::VertexLayoutHash(view.vertexAttributeMask);
    geometry.vertices.resize(6 * view.vertexStride);
    view.vertexData = geometry.vertices.data();
    view.vertexBytes = geometry.vertices.size();
    view.indexData = geometry.indices.data();
    view.indexCount = 6;
    geometry.draw.worldMatrix = math::matrix4x4::identity();
    const std::array<std::array<float, 2>, 3> ndc{{{-.8125f, -.75f}, {.8125f, -.625f}, {-.0625f, .8125f}}};
    const std::array<float, 3> depths{nearClip ? -.85f : -.35f, -.05f, .3f};
    const std::array<std::array<float, 2>, 6> uvs{
        {{.1f, .2f}, {.9f, .15f}, {.2f, .85f}, {.8f, .8f}, {.6f, .6f}, {.9f, .4f}}};
    for (unsigned i = 0; i < 6; ++i)
    {
        const unsigned corner = i % 3;
        const float z = depths[corner] + (i >= 3 ? .65f : 0);
        const float w = 1 + .5f * z;
        auto& point = geometry.points[i];
        point.position = {ndc[corner][0] * w, ndc[corner][1] * w, z, 0};
        point.uvLod = {uvs[i][0], uvs[i][1], 0, 0};
        const auto write = [&](assets::VertexAttribute attribute, const void* data, std::size_t bytes) {
            std::memcpy(geometry.vertices.data() + i * view.vertexStride +
                            assets::OffsetOf(view.vertexAttributeMask, attribute),
                        data, bytes);
        };
        write(assets::VertexAttribute::Position, point.position.data(), 12);
        write(assets::VertexAttribute::Normal, point.normal.data(), 12);
        const std::array<float, 4> tangent{1, 0, 0, 1};
        write(assets::VertexAttribute::Tangent, tangent.data(), 16);
        write(assets::VertexAttribute::Uv0, point.uvLod.data(), 8);
    }
    if (reverse)
    {
        std::swap(geometry.indices[1], geometry.indices[2]);
        std::swap(geometry.indices[4], geometry.indices[5]);
    }
    return geometry;
}

// Independent double reference: screen barycentrics -> reciprocal-W correction.
// Fine derivatives compare the two lane positions of the pixel's 2x2 quad.
struct Reference
{
    bool covered{};
    unsigned triangle{};
    SurfacePoint point;
    std::array<double, 4> derivatives{};
};

std::array<double, 3> Weights(double x, double y, const RasterSurfaceRequest& request)
{
    const double px = x / request.width * 2 - 1;
    const double py = 1 - y / request.height * 2;
    constexpr double ax = -.8125, ay = -.75, bx = .8125, by = -.625, cx = -.0625, cy = .8125;
    const double denominator = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    const double a = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / denominator;
    const double b = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / denominator;
    return {a, b, 1 - a - b};
}

SurfacePoint Interpolate(const Geometry& geometry, unsigned triangle, const std::array<double, 3>& weights)
{
    SurfacePoint point{};
    std::array<double, 20> result{};
    double denominator{};
    for (unsigned i = 0; i < 3; ++i)
    {
        const auto& vertex = geometry.points[triangle * 3 + i];
        const double reciprocal = weights[i] / (1 + .5 * vertex.position[2]);
        denominator += reciprocal;
        const auto values = std::bit_cast<std::array<float, 20>>(vertex);
        for (unsigned c = 0; c < 20; ++c)
        {
            result[c] += values[c] * reciprocal;
        }
    }
    std::array<float, 20> values;
    for (unsigned c = 0; c < 20; ++c)
    {
        values[c] = float(result[c] / denominator);
    }
    return std::bit_cast<SurfacePoint>(values);
}

Reference PixelReference(const Geometry& geometry, unsigned x, unsigned y, const RasterSurfaceRequest& request,
                         bool reverseOrder = false)
{
    Reference result;
    const auto weights = Weights(x + .5, y + .5, request);
    double nearest = 1;
    for (unsigned ordinal = 0; ordinal < 2; ++ordinal)
    {
        const unsigned triangle = reverseOrder ? 1 - ordinal : ordinal;
        double depth{};
        for (unsigned i = 0; i < 3; ++i)
        {
            const double z = geometry.points[triangle * 3 + i].position[2];
            depth += weights[i] * (.4 * z + .2) / (1 + .5 * z);
        }
        // D3D top-left ownership: for this clockwise screen triangle only CA is
        // inclusive. Pixels exactly on AB/BC belong to the adjacent primitive.
        const bool inside = weights[0] > 1e-12 && weights[1] >= -1e-12 && weights[2] > 1e-12;
        if (inside && depth >= 0 && depth < nearest)
        {
            result.covered = true;
            result.triangle = triangle;
            nearest = depth;
        }
    }
    if (!result.covered)
    {
        return result;
    }
    result.point = Interpolate(geometry, result.triangle, weights);
    const auto left = Interpolate(geometry, result.triangle, Weights((x & ~1u) + .5, y + .5, request));
    const auto right = Interpolate(geometry, result.triangle, Weights((x & ~1u) + 1.5, y + .5, request));
    const auto top = Interpolate(geometry, result.triangle, Weights(x + .5, (y & ~1u) + .5, request));
    const auto bottom = Interpolate(geometry, result.triangle, Weights(x + .5, (y & ~1u) + 1.5, request));
    for (unsigned c = 0; c < 2; ++c)
    {
        result.derivatives[c] = double(right.uvLod[c]) - left.uvLod[c];
        result.derivatives[c + 2] = double(bottom.uvLod[c]) - top.uvLod[c];
    }
    const double dx =
        std::hypot(result.derivatives[0] * request.texture.width, result.derivatives[1] * request.texture.height);
    const double dy =
        std::hypot(result.derivatives[2] * request.texture.width, result.derivatives[3] * request.texture.height);
    result.point.uvLod[3] = float(std::clamp(std::log2(std::max({dx, dy, 1e-20})) + request.texture.bias, 0.0,
                                             double(request.texture.mipLevels - 1)));
    return result;
}

class RecordingChangeDevice : public ProbeDevice
{
  public:
    bool flushOnUpload{};
      bool forbidAllocations{}, forbidImmediate{};
      bool trackOwnedResources{};
      unsigned textureAttempt{}, bufferAttempt{}, failTextureAt{}, failBufferAt{};
      std::set<std::uint32_t> ownedTextures, ownedBuffers;
      IRHIUploadTransactionListener* lastListener{};
      void RegisterUploadTransactionListener(IRHIUploadTransactionListener* listener) override
      {
          lastListener = listener;
          ProbeDevice::RegisterUploadTransactionListener(listener);
      }
      bool CreateTexture(const RHITextureDesc& desc, RHITextureHandle& result, std::string& error) override
      {
          if (trackOwnedResources && ++textureAttempt == failTextureAt)
          {
              result = {};
              error = "Injected texture allocation failure";
              return false;
          }
          const bool success = ProbeDevice::CreateTexture(desc, result, error);
          if (trackOwnedResources && result.IsValid())
          {
              ownedTextures.insert(result.id);
          }
          return success;
      }
      bool CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& result, std::string& error) override
      {
          if (trackOwnedResources && ++bufferAttempt == failBufferAt)
          {
              result = {};
              error = "Injected buffer allocation failure";
              return false;
          }
          const bool success = ProbeDevice::CreateBuffer(desc, result, error);
          if (trackOwnedResources && result.IsValid())
          {
              ownedBuffers.insert(result.id);
          }
          return success;
      }
      void ReleaseTexture(RHITextureHandle texture) override
      {
          ownedTextures.erase(texture.id);
          ProbeDevice::ReleaseTexture(texture);
      }
      void ReleaseBuffer(RHIBufferHandle buffer) override
      {
          ownedBuffers.erase(buffer.id);
          ProbeDevice::ReleaseBuffer(buffer);
      }
    RHIBufferSlice AllocateUpload(const RHIUploadRequest& request) override
    {
        if (forbidAllocations)
        {
            throw std::runtime_error("A graph callback allocated upload memory.");
        }
        if (flushOnUpload)
        {
            flushOnUpload = false;
            std::string error;
            Check(FlushCommandList(error), "Injected native prefix: " + error);
        }
        return ProbeDevice::AllocateUpload(request);
    }
    RHIBindingTable CreateBindings(std::span<const RHIBindingDesc> descriptions) override
    {
        if (forbidAllocations)
        {
            throw std::runtime_error("A graph callback allocated descriptors.");
        }
        return ProbeDevice::CreateBindings(descriptions);
    }
    RHIEncoder& GetImmediateEncoder() override
    {
        if (forbidImmediate)
        {
            throw std::runtime_error("A worker graph callback fetched the immediate encoder.");
        }
        return ProbeDevice::GetImmediateEncoder();
    }
};

struct Drain
{
    ProbeDevice& device;
    ~Drain()
    {
        if (device.GetCurrentUploadRecordingId() != 0)
        {
            device.AbortFrame();
        }
        std::string error;
        GetRHISubmissionThread().DrainSubmissions(&device, error);
        device.WaitForGpu();
    }
};

unsigned graphFrames{}, graphLists{}, graphFailures{};
void CheckReferenceDeclarations(const EnhancedRenderGraph& graph)
{
    if (graph.GetSchedulingMode() != RGSchedulingMode::ExplicitVersioned)
    {
        return;
    }
    EnhancedRenderGraph::DiagnosticSnapshot snapshot;
    Check(graph.CaptureDiagnosticSnapshot(snapshot), "Reference compiled diagnostic snapshot");
    for (const auto& pass : snapshot.passes)
    {
        for (const auto& usage : pass.usages)
        {
            Check(usage.access == RGAccessMode::Read || usage.access == RGAccessMode::Write,
                  "Reference graph has no implicit accesses");
            Check(usage.access != RGAccessMode::Write || usage.version == 1,
                  "Reference outputs use their produced version");
        }
    }
    Check(!snapshot.versionEdges.empty(), "Reference producer/consumer version dependencies are present");
}
void RunGraphChain(RecordingChangeDevice& device, ProbePool& pool, ProbeTextures& textures,
                   MeshSurfaceEvaluator& meshEvaluator, RenderBindingCache& bindings, RasterSurfaceCollector& collector,
                   SurfaceEvaluator& evaluator, IblBaker& baker, own::shared_owner<const Instance> instance,
                   own::shared_owner<const Texture> cube, const RasterSurfaceRequest& request,
                   std::span<const std::shared_ptr<const MeshSurfaceBatch>> sources,
                   std::span<const SurfacePoint> expectedGeometry, std::span<const IblBakePoint> expectedSurface,
                   std::span<const IblBakeSample> expectedBake, unsigned workers,
                   RGSchedulingMode scheduling = RGSchedulingMode::DeclarationOrder,
                   RGOrderPolicy order = RGOrderPolicy::DependencyOrder)
{
    std::string error;
    const auto count = request.width * request.height;
    std::shared_ptr<const RenderBindings> material;
    std::shared_ptr<const RasterSurfaceBatch> raster;
    std::shared_ptr<const SurfaceBatch> surface;
    std::shared_ptr<const IblBakeResult> baked;
    std::vector<std::shared_ptr<const MeshSurfaceBatch>> currentSources;
    std::vector<RHIReadback> meshReadbacks(sources.size());
    auto graph = std::make_shared<EnhancedRenderGraph>(device, scheduling, order);
    const auto read = scheduling == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read;
    std::array<RHIReadback, 3> readbacks;
    Check(device.CreateBufferReadback(expectedGeometry.size_bytes(), readbacks[0], error), "Graph geometry readback");
    Check(device.CreateBufferReadback(expectedSurface.size_bytes(), readbacks[1], error), "Graph surface readback");
    Check(device.CreateBufferReadback(expectedBake.size_bytes(), readbacks[2], error), "Graph IBL readback");
    for (std::size_t i = 0; i < sources.size(); ++i)
    {
        Check(device.CreateBufferReadback(sources[i]->Count() * sizeof(SurfacePoint), meshReadbacks[i], error),
              "Current mesh readback");
    }
    Drain drain{device};
    Check(device.BeginFrame(error), "Graph chain begin");
    textures.BeginFrame(100 + graphFrames);
    // Touch/upload before the prefix. Current-pose mesh packets, rather than
    // previously submitted geometry buffers, are prepared after this boundary.
    const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), 1, cube};
    for (const auto& texture : instance->textures)
    {
        Check(textures.GetOrUpload((texture.owner ? &*texture.owner.borrow() : nullptr), texture.owner ? texture.owner->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error).IsValid(), "Touch graph material texture");
    }
    if (workers)
    {
        pool.BeginFrame(graphFrames);
        Check(graph->PrepareParallel(pool, error), "Open graph recording " + error);
        Check(!graph->PrepareParallel(pool, error), "Duplicate graph preparation rejected");
    }
    for (const auto& source : sources)
    {
        std::shared_ptr<const MeshSurfaceBatch> current;
        Check(meshEvaluator.Prepare(device, source->Input(), current, error), "Current mesh preparation " + error);
        Check(!current->IsReadyForEvaluation() && !current->IsValidated(),
              "Preparation is not mesh recording/acceptance");
        currentSources.push_back(std::move(current));
    }
    Check(bindings.Prepare(device, textures, instance, evaluator.Layout(), material, error), "Graph bindings " + error);
    Check(collector.Prepare(device, request, currentSources, raster, error), "Graph raster preparation " + error);
    Check(evaluator.PrepareGpu(device, material, raster, surface, error), "Graph surface preparation " + error);
    Check(baker.PrepareGpu(device, environment, surface, baked, error), "Graph bake preparation " + error);
    auto retainedBake = baked;
    Check(!baker.RecordGpu(device, environment, surface, baked, error) && baked == retainedBake,
          "An unrecorded surface cannot become an immediate IBL source");
    retainedBake.reset();
    Check(!surface->Declare(*graph, error) && !baked->Declare(*graph, error), "Consumers reject missing producers");
    Check(!raster->Declare(*graph, error),
          "Raster rejects missing current mesh producer without consuming declaration");
    for (const auto& source : currentSources)
    {
        Check(source->Declare(*graph, error), "Current mesh declaration " + error);
        Check(!source->Declare(*graph, error), "Mesh declaration is single use");
    }
    Check(raster->Declare(*graph, error), "Graph raster declaration " + error);
    EnhancedRenderGraph wrongGraph(device);
    std::shared_ptr<const RasterSurfaceBatch> wrongRaster;
    Check(collector.Prepare(device, request, currentSources, wrongRaster, error) &&
              !wrongRaster->Declare(wrongGraph, error),
          "Current geometry cannot cross producer graph owners");
    Check(!surface->Declare(wrongGraph, error), "A graph buffer cannot cross graph owners");
    Check(surface->Declare(*graph, error) && baked->Declare(*graph, error), "Graph consumers " + error);
    Check(!raster->Declare(*graph, error) && !surface->Declare(*graph, error) && !baked->Declare(*graph, error),
          "Duplicate graph packets rejected");
    const std::array owners{raster->Buffer(), surface->Buffer(), baked->Buffer()};
    const std::array handles{raster->GraphOutput(*graph), surface->GraphOutput(*graph), baked->GraphOutput(*graph)};
    for (unsigned i = 0; i < 3; ++i)
    {
        Check(handles[i].IsValid() && graph->ResolveBufferHandle(handles[i]) == owners[i],
              "Exact graph buffer identity");
        graph->AddPass(
            "Probe.Readback", {{handles[i], RHIResourceState::CopySource, read}},
            [readback = readbacks[i], buffer = owners[i]](const auto& context) {
                context.encoder->CopyBufferToReadback(readback, buffer);
            },
            true);
    }
    for (std::size_t i = 0; i < currentSources.size(); ++i)
    {
        const auto source = currentSources[i];
        graph->AddPass(
            "Probe.MeshReadback", {{source->GraphOutput(*graph), RHIResourceState::CopySource, read}},
            [source, readback = meshReadbacks[i]](const auto& context) {
                context.encoder->CopyBufferToReadback(readback, source->Buffer());
            },
            true);
    }
    Check(graph->Compile(error), "Graph chain compile " + error);
    CheckReferenceDeclarations(*graph);
    Check(graph->GetStats().passesExecuted == 10 + 3 * currentSources.size(),
          "Current mesh and all consumer/readback passes are retained");
    device.forbidAllocations = true;
    device.forbidImmediate = workers != 0;
    struct RestoreGuards
    {
        RecordingChangeDevice& device;
        ~RestoreGuards() { device.forbidAllocations = device.forbidImmediate = false; }
    } restore{device};
    RHISubmissionTicket ticket;
    if (workers)
    {
        graph->SetParallelRecordCostThreshold(0);
        RHIRecordedBatchDesc description;
        description.frameId = 100 + graphFrames;
        description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
        description.lifetimeToken = graph;
        RHIRecordedBatch batch;
        const auto recording = device.GetCurrentUploadRecordingId();
        const bool recorded = graph->RecordParallel(pool, workers, description, batch, error);
        device.forbidAllocations = device.forbidImmediate = false;
        Check(recorded, "Graph worker recording " + error);
        Check(device.GetCurrentUploadRecordingId() == recording, "Prepared graph does not flush a second prefix");
        Check(batch.GetCommandCount() == workers && batch.HasLifetimeToken(), "Native worker lists and lifetime token");
        graphLists += batch.GetCommandCount();
        RHIRecordedBatch duplicate;
        Check(!graph->RecordParallel(pool, workers, description, duplicate, error), "Prepared recording is single use");
        Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
              "Native graph batch enqueue " + error);
    }
    else
    {
        const bool recorded = graph->Execute(error);
        device.forbidAllocations = false;
        Check(recorded, "Sequential graph recording " + error);
    }
    Check(raster->IsReadyForEvaluation() && surface->IsReadyForBake() && baked->IsReady(),
          "All graph callbacks recorded");
    std::weak_ptr<const IblBakeResult> ownedByGraph = baked;
    if (workers)
    {
        // The submission token, rather than caller-local owners, keeps the graph
        // and its entire source/material/environment chain alive through completion.
        graph.reset();
        raster.reset();
        surface.reset();
        baked.reset();
        material.reset();
        // Enqueue transfers the token from the ticket's batch to GPU retirement.
        Check(!ownedByGraph.expired(), "GPU retirement retained graph");
        ticket = {};
        Check(!ownedByGraph.expired(), "Graph lifetime is independent of submission ticket");
    }
    Check(device.EndFrame(error), "Graph chain frame submit " + error);
    Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Graph chain native submission " + error);
    device.WaitForGpu();
    Check(GetRHISubmissionThread().Drain(&device, error), "Graph retirement " + error);
    Check(!workers || ownedByGraph.expired(), "GPU completion releases submitted graph owners");
    std::array<RHIReadbackImage, 3> mapped;
    for (unsigned i = 0; i < 3; ++i)
    {
        Check(device.MapReadback(readbacks[i], mapped[i], error), "Graph chain readback map");
    }
    for (std::size_t i = 0; i < currentSources.size(); ++i)
    {
        RHIReadbackImage meshMapped;
        const auto& source = currentSources[i];
        Check(source->IsReadyForEvaluation(), "Current mesh graph was recorded");
        Check(device.MapReadback(meshReadbacks[i], meshMapped, error) &&
                  source->ValidateReadback({meshMapped.Elements<SurfacePoint>(), source->Count()}, error),
              "Current mesh completion acceptance " + error);
        device.ReleaseReadback(meshReadbacks[i]);
    }
    Check(std::memcmp(mapped[0].Elements<SurfacePoint>(), expectedGeometry.data(), expectedGeometry.size_bytes()) == 0,
          "Graph raster is byte-identical to independent-CPU-validated baseline");
    Check(std::memcmp(mapped[1].Elements<IblBakePoint>(), expectedSurface.data(), expectedSurface.size_bytes()) == 0,
          "Graph material is byte-identical to independent-CPU-validated baseline");
    Check(std::memcmp(mapped[2].Elements<IblBakeSample>(), expectedBake.data(), expectedBake.size_bytes()) == 0,
          "Graph IBL is byte-identical to independent-CPU-validated baseline");
    if (!workers)
    {
        Check(raster->ValidateReadback({mapped[0].Elements<SurfacePoint>(), count}, error), "Graph raster acceptance");
        Check(surface->ValidateReadback({mapped[1].Elements<IblBakePoint>(), count}, error),
              "Graph surface acceptance");
        graph->Reset();
        for (const auto& source : currentSources)
        {
            Check(!source->GraphOutput(*graph).IsValid(), "Reset invalidates current mesh output handles");
        }
        Check(!raster->GraphOutput(*graph).IsValid() && !surface->GraphOutput(*graph).IsValid() &&
                  !baked->GraphOutput(*graph).IsValid(),
              "Reset invalidates resource references");
    }
    std::string messages;
    const auto errors = device.DrainDebugMessages(messages);
    Check(errors == 0, "Graph GPU validation: " + messages);
    for (auto readback : readbacks)
    {
        device.ReleaseReadback(readback);
    }
    ++graphFrames;
}

unsigned sharedDepthFrames{}, sharedDepthPixels{}, coplanarPixels{}, skinnedDepthFrames{}, meshFailures{};
unsigned sceneInputFrames{}, sceneInputFailures{};

void RunSceneInputFailures(const std::array<own::shared_owner<const Instance>, 2>& instances)
{
    auto currentInstance = instances[0];
    auto source = SceneMaterialSource::Capture(currentInstance, SceneCoverage::Opaque, true);
    currentInstance = instances[1];
    const auto replacement = SceneMaterialSource::Capture(currentInstance, SceneCoverage::Masked, false);
    Check(source && replacement && material_graph_test::SamePinnedObject(source->instance, instances[0]) && material_graph_test::SamePinnedObject(replacement->instance, instances[1]) &&
              (source->coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0 &&
              (replacement->coverage.flags & EnhancedMaterialCoverage::Masked) != 0 &&
              (replacement->coverage.flags & EnhancedMaterialCoverage::DoubleSided) == 0,
          "Producer graph replacement preserves prior instance/coverage snapshot");
    Check(!SceneMaterialSource::Capture({}, SceneCoverage::Opaque, false),
          "Producer graph removal explicitly clears the source");
    const auto invalidSource = SceneMaterialSource::Capture(instances[0], static_cast<SceneCoverage>(255), false);
    Check(invalidSource && material_graph_test::SamePinnedObject(invalidSource->instance, instances[0]) && !invalidSource->coverage.flags,
          "Invalid producer coverage keeps graph ownership and cannot choose legacy");
    auto geometry = MakeGeometry(false, false);
    auto& draw = geometry.draw;
    draw.modelMeshView.indexData = geometry.indices.data();
    draw.geometryKey = 11;
    draw.materialGraphInstance = source->instance;
    draw.coverage = source->coverage;
    source.reset();
    SceneInputView view;
    view.frameId = 10;
    view.sceneEpoch = 2;
    view.viewId = 1;
    view.width = view.height = 32;
    view.camera.view = view.camera.projection = math::matrix4x4::identity();
    view.camera.eyePosition = {0, 0, 2};
    std::shared_ptr<const SceneViewInput> accepted;
    std::string error;
    Check(SceneViewInput::Seal(view, {&draw, 1}, {}, accepted, error), "Scene input seal " + error);
    {
        const std::array repeated{draw, draw};
        std::shared_ptr<const SceneViewInput> sharedInput;
        Check(SceneViewInput::Seal(view, repeated, {}, sharedInput, error) &&
            sharedInput->MaterialPins()->Size() == 1 &&
            sharedInput->Draws()[0].materialPinIndex == sharedInput->Draws()[1].materialPinIndex,
            "Repeated Scene draws share one immutable instance pin and store only indices/views");
    }
    Check(accepted->Draws().size() == 1 && material_graph_test::SamePinnedObject(accepted->Draws()[0].material, instances[0]) &&
              accepted->View().frameId == 10 && accepted->View().sceneEpoch == 2,
          "Scene frame retains its exact typed instance and identity");
    const auto& firstMesh = accepted->Draws()[0].geometry->Chunks()[0].input;
    const auto& sealedGeometry = firstMesh->Geometry();
    const auto* sealedBytes = static_cast<const std::byte*>(sealedGeometry.vertexData);
    const std::vector expectedBytes(sealedBytes, sealedBytes + sealedGeometry.vertexBytes);
    auto otherView = view;
    otherView.viewId = 2;
    otherView.historyRevision = 7;
    otherView.camera.eyePosition.x = .3f;
    otherView.camera.projection.m[0][0] = 2;
    std::shared_ptr<const SceneViewInput> second;
    Check(SceneViewInput::Seal(otherView, {&draw, 1}, {}, second, error), "Second Scene view");
    Check(second->Surface().viewRevision != accepted->Surface().viewRevision &&
              second->Surface().geometryRevision != accepted->Surface().geometryRevision &&
              second->Surface().eye[0] == .3f && accepted->Surface().eye[0] == 0 &&
              second->ViewProjection().m[0][0] == 2 && accepted->ViewProjection().m[0][0] == 1,
          "Same-frame Scene/Game camera and geometry revisions cannot mix");
    draw.worldMatrix.m[3][0] = 3;
    draw.materialGraphInstance = instances[1];
    std::shared_ptr<const SceneViewInput> changed;
    Check(SceneViewInput::Seal(view, {&draw, 1}, {}, changed, error), "Changed world/instance seal");
    Check(changed->Surface().geometryRevision != accepted->Surface().geometryRevision &&
              material_graph_test::SamePinnedObject(changed->Draws()[0].material, instances[1]) && material_graph_test::SamePinnedObject(accepted->Draws()[0].material, instances[0]) &&
              changed->Draws()[0].geometry->Chunks()[0].input->World().m[3][0] == 3 && firstMesh->World().m[3][0] == 0,
          "World or instance replacement does not relabel the previous snapshot");
    std::fill(geometry.vertices.begin(), geometry.vertices.end(), std::byte{});
    std::fill(geometry.indices.begin(), geometry.indices.end(), UINT32_MAX);
    Check(std::memcmp(sealedGeometry.vertexData, expectedBytes.data(), expectedBytes.size()) == 0 &&
              sealedGeometry.indexData[0] == 0 && firstMesh->World().m[3][0] == 0,
          "Sealed Scene geometry survives mutable producer edits");
    geometry = MakeGeometry(false, false);
    geometry.draw.modelMeshView.indexData = geometry.indices.data();
    geometry.draw.geometryKey = 11;
    geometry.draw.materialGraphInstance = instances[0];
    geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
    for (unsigned failure = 0; failure < 24; ++failure)
    {
        auto invalidView = view;
        SceneInputBudget budget;
        std::vector invalidDraws{geometry.draw};
        auto& invalid = invalidDraws.front();
        switch (failure)
        {
        case 0:
            invalidView.frameId = 0;
            break;
        case 1:
            invalidView.sceneEpoch = 0;
            break;
        case 2:
            invalidView.viewId = 0;
            break;
        case 3:
            invalidView.width = 0;
            break;
        case 4:
            invalidView.height = 0;
            break;
        case 5:
            invalidView.width = invalidView.height = UINT32_MAX;
            break;
        case 6:
            invalidView.camera.eyePosition.x = std::numeric_limits<float>::quiet_NaN();
            break;
        case 7:
            invalidView.camera.projection = {};
            break;
        case 8:
            invalidView.camera.view.m[0][0] = std::numeric_limits<float>::infinity();
            break;
        case 9:
            invalid.geometryKey = 0;
            break;
        case 10:
            invalid.materialGraphInstance.reset();
            break;
        case 11: {
            Instance instanceValue(*instances[0]);
            instanceValue.generation.reset();
            invalid.materialGraphInstance = own::make_shared<const Instance>(std::move(instanceValue));
            break;
        }
        case 12: {
            Instance instanceValue(*instances[0]);
            instanceValue.description.graphId = {};
            invalid.materialGraphInstance = own::make_shared<const Instance>(std::move(instanceValue));
            break;
        }
        case 13:
            invalid.materialSnapshot = std::make_shared<EnhancedMaterialDrawSnapshot>();
            break;
        case 14:
            invalid.forwardMaterialSnapshot = std::make_shared<EnhancedForwardMaterialDrawSnapshot>();
            break;
        case 15:
            invalid.coverage.flags = 0;
            break;
        case 16:
            invalid.coverage.flags |= EnhancedMaterialCoverage::Masked | EnhancedMaterialCoverage::Blended;
            break;
        case 17:
            invalid.modelMeshView.indexData = nullptr;
            break;
        case 18:
            budget.mesh.maxSourceVertices = 5;
            break;
        case 19:
            budget.draws = 0;
            break;
        case 20:
            budget.cpuPayloadBytes = accepted->Cost().cpuPayloadBytes - 1;
            break;
        case 21:
            budget.gpuPayloadBytes = accepted->Cost().gpuPayloadBytes - 1;
            break;
        case 22:
            invalidDraws.push_back(invalid);
            invalidDraws.back().modelMeshView.indexData = nullptr;
            break;
        case 23:
            invalidDraws.push_back(invalid);
            budget.chunks = 1;
            break;
        }
        const auto previous = accepted;
        Check(!SceneViewInput::Seal(invalidView, invalidDraws, budget, accepted, error) && accepted == previous &&
                  !error.empty(),
              "Scene input failure preserves the complete previous view fixture=" + std::to_string(failure));
        ++sceneInputFailures;
    }
    // These malformed inputs must fail before bounds reads any source/palette byte.
    const std::array<std::byte, 1> tinyVertex{};
    const auto oneBone = math::matrix4x4::identity();
    for (unsigned invalidCase = 0; invalidCase < 2; ++invalidCase)
    {
        auto invalid = geometry.draw;
        if (invalidCase == 0)
        {
            invalid.modelMeshView.vertexData = tinyVertex.data();
            invalid.modelMeshView.vertexBytes = 1;
            invalid.modelMeshView.vertexStride = 1;
        }
        else
        {
            invalid.boneCount = 257;
            invalid.bonePalette = &oneBone;
        }
        const auto previous = accepted;
        Check(!SceneViewInput::Seal(view, {&invalid, 1}, {}, accepted, error)
                  && accepted == previous && !error.empty(),
              "Malformed shadow bounds input rejected before dereference");
    }
    FrameCameraSnapshot selectionCamera;
    selectionCamera.projection = math::perspective_fov_lh(1.f, 1.f, .1f, 100.f);
    selectionCamera.inverseView = math::matrix4x4::identity();
    selectionCamera.nearPlane = .1f; selectionCamera.farPlane = 100.f;
    const math::vector3 selectionDirection{0, -1, 0};
    const auto receivers = shadow_math::ReceiverCascades(selectionCamera, selectionDirection, 200, .15f);
    std::vector<EnhancedDrawItem> selectedDraws;
    for (unsigned i = 0; i < 4100; ++i)
    {
        auto candidate = geometry.draw;
        candidate.boundRadius = 2;
        candidate.worldMatrix = math::matrix4x4::identity();
        candidate.worldMatrix.m[3][2] = 5;
        if (i == 1) candidate.worldMatrix.m[3][1] = 1000; // offscreen caster, retained
        if (i > 1) candidate.worldMatrix.m[3][0] = 10000.f + float(i); // unrelated, rejected
        if (shadow_math::RelevantToView(i == 0, shadow_math::WorldBounds(candidate), receivers, selectionDirection, true))
            selectedDraws.push_back(candidate);
    }
    std::shared_ptr<const SceneViewInput> selectedInput;
    Check(selectedDraws.size() == 2 && SceneViewInput::Seal(view, selectedDraws, {}, selectedInput, error)
              && selectedInput->Draws().size() == 2,
          "Pre-budget union keeps visible/offscreen caster but rejects 4098 unrelated graph draws");
    std::cout << "CSM_SELECTION_BUDGET_OK candidates=4100 sealed=2\n";
    auto coverageDraw = geometry.draw;
    coverageDraw.coverage.flags |= EnhancedMaterialCoverage::Masked;
    std::shared_ptr<const SceneViewInput> masked;
    Check(SceneViewInput::Seal(view, {&coverageDraw, 1}, {}, masked, error) &&
              masked->Draws()[0].queue == SceneCoverage::Masked,
          "Input capture preserves masked coverage without declaring a raster transport");
    coverageDraw.coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::Blended;
    std::shared_ptr<const SceneViewInput> blended;
    Check(SceneViewInput::Seal(view, {&coverageDraw, 1}, {}, blended, error) &&
              blended->Draws()[0].queue == SceneCoverage::Blended,
          "Input coverage queue is independent of physical material route");
    std::shared_ptr<const SceneViewInput> empty;
    ++view.sceneEpoch;
    Check(SceneViewInput::Seal(view, {}, {}, empty, error) && empty->Draws().empty() && empty->Cost().chunks == 0 &&
              empty->View().sceneEpoch == 3,
          "Empty/new Scene view cannot retain old draws");
}
void RunSharedDepthChain(RecordingChangeDevice& device, ProbePool& pool, ProbeTextures& textures,
                         MeshSurfaceEvaluator& meshEvaluator, RenderBindingCache& bindings,
                         RasterSurfaceCollector& collector, std::array<SurfaceEvaluator, 2>& evaluators,
                         IblBaker& baker, const std::array<own::shared_owner<const Instance>, 2>& instances,
                         own::shared_owner<const Texture> cube, const Environment& colors, const SheenTable& table,
                         unsigned fixture, bool reverseOrder, unsigned workers,
                         RGSchedulingMode scheduling = RGSchedulingMode::DeclarationOrder,
                         RGOrderPolicy order = RGOrderPolicy::DependencyOrder)
{
    std::string error;
    auto geometry = MakeGeometry(fixture == 1, false);
    if (fixture == 2)
    {
        for (unsigned i = 0; i < 3; ++i)
        {
            geometry.points[i + 3].position = geometry.points[i].position;
            std::memcpy(geometry.vertices.data() + (i + 3) * geometry.draw.modelMeshView.vertexStride,
                        geometry.points[i].position.data(), 12);
        }
    }
    std::array<math::matrix4x4, 2> bones{math::matrix4x4::identity(), math::matrix4x4::identity()};
    if (fixture == 3)
    {
        const auto oldMask = geometry.draw.modelMeshView.vertexAttributeMask;
        const auto oldStride = geometry.draw.modelMeshView.vertexStride;
        const auto skinMask = oldMask | assets::kSkinVertexAttributes;
        const auto skinStride = assets::StrideOf(skinMask);
        std::vector<std::byte> skinned(6 * skinStride);
        bones[0].m[3][0] = .07f * (workers + 1);
        bones[1].m[3][1] = reverseOrder ? -.13f : .09f;
        for (unsigned i = 0; i < 6; ++i)
        {
            for (const auto& attribute : assets::kVertexAttributeTable)
            {
                if (assets::Has(oldMask, attribute.attribute))
                {
                    std::memcpy(skinned.data() + i * skinStride + assets::OffsetOf(skinMask, attribute.attribute),
                                geometry.vertices.data() + i * oldStride +
                                    assets::OffsetOf(oldMask, attribute.attribute),
                                assets::SizeOf(attribute.format));
                }
            }
            const unsigned bone = i % 2;
            auto position = geometry.points[i].position;
            position[0] -= bones[bone].m[3][0];
            position[1] -= bones[bone].m[3][1];
            const std::array<std::uint8_t, 4> indices{static_cast<std::uint8_t>(bone), 0, 0, 0};
            const std::array<float, 4> weights{1, 0, 0, 0};
            std::memcpy(skinned.data() + i * skinStride + assets::OffsetOf(skinMask, assets::VertexAttribute::Position),
                        position.data(), 12);
            std::memcpy(skinned.data() + i * skinStride +
                            assets::OffsetOf(skinMask, assets::VertexAttribute::BoneIndices),
                        indices.data(), sizeof(indices));
            std::memcpy(skinned.data() + i * skinStride +
                            assets::OffsetOf(skinMask, assets::VertexAttribute::BoneWeights),
                        weights.data(), sizeof(weights));
        }
        geometry.vertices = std::move(skinned);
        auto& meshView = geometry.draw.modelMeshView;
        meshView.vertexAttributeMask = skinMask;
        meshView.vertexStride = skinStride;
        meshView.vertexLayoutHash = assets::VertexLayoutHash(skinMask);
        meshView.vertexBytes = geometry.vertices.size();
        meshView.vertexData = geometry.vertices.data();
        geometry.draw.boneCount = 2;
        geometry.draw.bonePalette = bones.data();
    }
    geometry.draw.modelMeshView.indexData = geometry.indices.data();
    geometry.draw.modelMeshView.indexCount = 3;
    auto second = geometry.draw;
    second.modelMeshView.indexData += 3;
    SceneInputView selectedView;
    selectedView.frameId = 300 + sharedDepthFrames;
    selectedView.sceneEpoch = 1;
    selectedView.viewId = 1 + workers;
    selectedView.historyRevision = 2;
    selectedView.width = 32;
    selectedView.height = 24;
    selectedView.camera.eyePosition = {.1f, .2f, 2};
    selectedView.camera.view = selectedView.camera.projection = math::matrix4x4::identity();
    selectedView.camera.projection.m[2][2] = .4f;
    selectedView.camera.projection.m[2][3] = .5f;
    selectedView.camera.projection.m[3][2] = .2f;
    std::array sourceDraws{geometry.draw, second};
    for (unsigned i = 0; i < sourceDraws.size(); ++i)
    {
        sourceDraws[i].geometryKey = i + 1;
        sourceDraws[i].materialGraphInstance = instances[i];
        sourceDraws[i].coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
    }
    std::shared_ptr<const SceneViewInput> sceneInput;
    Check(SceneViewInput::Seal(selectedView, sourceDraws, {}, sceneInput, error), "Selected Scene view seal " + error);
    Check(sceneInput->Draws().size() == 2 && sceneInput->Cost().chunks == 2,
          "Selected Scene view retains both material partitions");
    const auto view = sceneInput->Surface();
    std::array<std::shared_ptr<const MeshSurfaceInput>, 2> inputs;
    for (unsigned i = 0; i < inputs.size(); ++i)
    {
        inputs[i] = sceneInput->Draws()[i].geometry->Chunks()[0].input;
        Check(material_graph_test::SamePinnedObject(sceneInput->Draws()[i].material, instances[i]) && inputs[i]->Count() == 3,
              "Scene instance identity and referenced triangle partition");
    }
    ++sceneInputFrames;
    if (fixture == 3)
    {
        // Sealed current-pose packets survive mutable source/palette edits.
        bones[0].m[3][0] = 999;
        std::fill(geometry.vertices.begin(), geometry.vertices.end(), std::byte{});
    }
    RasterSurfaceRequest request;
    request.width = sceneInput->View().width;
    request.height = sceneInput->View().height;
    request.viewProjection = sceneInput->ViewProjection();
    request.texture = {{}, {}, 4, 2, 2, 4};
    const auto count = request.width * request.height;
    std::array<RHIReadback, 9> readbacks;
    const std::array<std::uint64_t, 9> bytes{inputs[0]->Count() * sizeof(SurfacePoint),
                                             inputs[1]->Count() * sizeof(SurfacePoint),
                                             count * sizeof(SurfacePoint),
                                             count * sizeof(SurfacePoint),
                                             count * sizeof(SurfacePoint),
                                             count * sizeof(IblBakePoint),
                                             count * sizeof(IblBakePoint),
                                             count * sizeof(IblBakeSample),
                                             count * sizeof(IblBakeSample)};
    for (unsigned i = 0; i < readbacks.size(); ++i)
    {
        Check(device.CreateBufferReadback(bytes[i], readbacks[i], error), "Shared buffer readback");
    }
    std::array<RHIReadback, 2> depthReadbacks;
    for (auto& readback : depthReadbacks)
    {
        Check(device.CreateReadback(request.width, request.height, RHIFormat::D32Float, 1, readback, error),
              "Shared depth readback " + error);
    }
    auto graph = std::make_shared<EnhancedRenderGraph>(device, scheduling, order);
    const auto read = scheduling == RGSchedulingMode::DeclarationOrder ? RGAccessMode::LegacyState : RGAccessMode::Read;
    std::array<std::shared_ptr<const MeshSurfaceBatch>, 2> meshes;
    std::array<std::shared_ptr<const RasterSurfaceBatch>, 2> raster;
    std::array<std::shared_ptr<const SurfaceBatch>, 2> surface;
    std::array<std::shared_ptr<const IblBakeResult>, 2> bake;
    std::array<std::shared_ptr<const RenderBindings>, 2> material;
    std::shared_ptr<const RasterSurfaceBatch> depth;
    std::array materialRequests{request, request};
    materialRequests[0].texture.bias = 0;
    Drain drain{device};
    Check(device.BeginFrame(error), "Shared graph begin");
    textures.BeginFrame(300 + sharedDepthFrames);
    const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), 1, cube};
    for (const auto& instance : instances)
    {
        for (const auto& texture : instance->textures)
        {
            Check(textures.GetOrUpload((texture.owner ? &*texture.owner.borrow() : nullptr), texture.owner ? texture.owner->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error).IsValid(), "Shared texture residency");
        }
    }
    if (workers)
    {
        pool.BeginFrame(sharedDepthFrames);
        Check(graph->PrepareParallel(pool, error), "Shared prefix boundary " + error);
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(meshEvaluator.Prepare(device, inputs[i], meshes[i], error), "Shared current mesh " + error);
    }
    const std::array ordered{meshes[reverseOrder ? 1 : 0], meshes[reverseOrder ? 0 : 1]};
    Check(collector.Prepare(device, request, ordered, depth, error), "Shared complete opaque prepass " + error);
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(collector.PrepareSharedDepth(device, materialRequests[i], {&meshes[i], 1}, depth, raster[i], error),
              "Shared material capture " + error);
        Check(raster[i]->Depth() == depth->Depth(), "Shared depth has exactly one owner/resource");
        Check(bindings.Prepare(device, textures, instances[i], evaluators[i].Layout(), material[i], error) &&
                  evaluators[i].PrepareGpu(device, material[i], raster[i], surface[i], error) &&
                  baker.PrepareGpu(device, environment, surface[i], bake[i], error),
              "Shared evaluation preparation " + error);
    }
    const auto accepted = raster[0];
    Check(!collector.PrepareSharedDepth(device, request, {&meshes[0], 1}, {}, raster[0], error) &&
              raster[0] == accepted,
          "Missing shared owner preserves prior capture");
    std::shared_ptr<const MeshSurfaceBatch> foreignMesh;
    Check(meshEvaluator.Prepare(device, inputs[0], foreignMesh, error), "Foreign mesh preparation");
    Check(!collector.PrepareSharedDepth(device, request, {&foreignMesh, 1}, depth, raster[0], error) &&
              raster[0] == accepted,
          "A different mesh owner cannot borrow another draw's shared coverage");
    auto wrong = request;
    wrong.viewProjection.m[3][0] += .1f;
    Check(!collector.PrepareSharedDepth(device, wrong, {&meshes[0], 1}, depth, raster[0], error) &&
              raster[0] == accepted,
          "Shared camera mismatch preserves prior capture");
    wrong = request;
    ++wrong.width;
    Check(!collector.PrepareSharedDepth(device, wrong, {&meshes[0], 1}, depth, raster[0], error) &&
              raster[0] == accepted,
          "Shared viewport mismatch preserves prior capture");
    Check(!collector.PrepareSharedDepth(device, request, {&meshes[0], 1}, raster[1], raster[0], error) &&
              raster[0] == accepted,
          "Shared depth cannot chain through a partial capture");
    Check(!raster[0]->Declare(*graph, error), "Missing opaque producer is rejected before mutation");
    for (const auto& mesh : meshes)
    {
        Check(mesh->Declare(*graph, error), "Shared current mesh declaration");
    }
    Check(depth->Declare(*graph, error), "Shared depth declaration");
    const auto depthHandle = depth->GraphDepth(*graph);
    graph->AddPass(
        "Probe.DepthBefore", {{depthHandle, RHIResourceState::CopySource, read}},
        [depth, readback = depthReadbacks[0]](const auto& context) {
            context.encoder->CopyToReadback(readback, depth->Depth());
        },
        true);
    EnhancedRenderGraph wrongGraph(device);
    Check(!raster[0]->Declare(wrongGraph, error), "Shared depth cannot cross graph owners");
    for (unsigned ordinal = 0; ordinal < 2; ++ordinal)
    {
        const auto i = reverseOrder ? 1 - ordinal : ordinal;
        Check(raster[i]->Declare(*graph, error) && surface[i]->Declare(*graph, error) &&
                  bake[i]->Declare(*graph, error),
              "Shared material declaration " + error);
    }
    const std::array buffers{meshes[0]->Buffer(),  meshes[1]->Buffer(), depth->Buffer(),
                             raster[0]->Buffer(),  raster[1]->Buffer(), surface[0]->Buffer(),
                             surface[1]->Buffer(), bake[0]->Buffer(),   bake[1]->Buffer()};
    const std::array handles{meshes[0]->GraphOutput(*graph), meshes[1]->GraphOutput(*graph), depth->GraphOutput(*graph),
                            raster[0]->GraphOutput(*graph), raster[1]->GraphOutput(*graph), surface[0]->GraphOutput(*graph),
                            surface[1]->GraphOutput(*graph), bake[0]->GraphOutput(*graph), bake[1]->GraphOutput(*graph)};
    for (unsigned i = 0; i < buffers.size(); ++i)
    {
        graph->AddPass(
            "Probe.SharedReadback", {{handles[i], RHIResourceState::CopySource, read}},
            [buffer = buffers[i], readback = readbacks[i]](const auto& context) {
                context.encoder->CopyBufferToReadback(readback, buffer);
            },
            true);
    }
    graph->AddPass(
        "Probe.DepthAfter", {{depthHandle, RHIResourceState::CopySource, read}},
        [depth, readback = depthReadbacks[1]](const auto& context) {
            context.encoder->CopyToReadback(readback, depth->Depth());
        },
        true);
    Check(graph->Compile(error), "Shared graph compile " + error);
    CheckReferenceDeclarations(*graph);
    device.forbidAllocations = true;
    device.forbidImmediate = workers != 0;
    RHISubmissionTicket ticket;
    bool recorded;
    if (workers)
    {
        graph->SetParallelRecordCostThreshold(0);
        RHIRecordedBatchDesc description;
        description.frameId = 300 + sharedDepthFrames;
        description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
        description.lifetimeToken = graph;
        RHIRecordedBatch batch;
        recorded = graph->RecordParallel(pool, workers, description, batch, error);
        device.forbidAllocations = device.forbidImmediate = false;
        Check(recorded &&
                  GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error),
              "Shared native worker submit " + error);
    }
    else
    {
        recorded = graph->Execute(error);
        device.forbidAllocations = device.forbidImmediate = false;
        Check(recorded, "Shared sequential record " + error);
    }
    Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error), "Shared frame submit");
    device.WaitForGpu();
    Check(GetRHISubmissionThread().Drain(&device, error), "Shared native retirement");
    std::array<RHIReadbackImage, 9> mapped;
    for (unsigned i = 0; i < mapped.size(); ++i)
    {
        Check(device.MapReadback(readbacks[i], mapped[i], error), "Shared map");
    }
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto& remap = sceneInput->Draws()[i].geometry->Chunks()[0].sourceVertices;
        for (unsigned vertex = 0; vertex < inputs[i]->Count(); ++vertex)
        {
            const auto expected = std::bit_cast<std::array<float, 20>>(geometry.points[remap[vertex]]);
            const auto actual = std::bit_cast<std::array<float, 20>>(mapped[i].Elements<SurfacePoint>()[vertex]);
            for (unsigned c = 0; c < actual.size(); ++c)
            {
                Near(actual[c], expected[c], "Current-pose world transform matches independent CPU geometry");
            }
        }
        Check(!raster[i]->ValidateReadback({mapped[i + 3].Elements<SurfacePoint>(), count}, error),
              "Shared publication awaits every prepass source");
        Check(meshes[i]->ValidateReadback({mapped[i].Elements<SurfacePoint>(), inputs[i]->Count()}, error),
              "Shared mesh acceptance");
    }
    Check(depth->ValidateReadback({mapped[2].Elements<SurfacePoint>(), count}, error), "Shared opaque acceptance");
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(raster[i]->ValidateReadback({mapped[i + 3].Elements<SurfacePoint>(), count}, error) &&
                  surface[i]->ValidateReadback({mapped[i + 5].Elements<IblBakePoint>(), count}, error),
              "Shared material acceptance " + error);
    }
    std::array<RHIReadbackImage, 2> depthMapped;
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(device.MapReadback(depthReadbacks[i], depthMapped[i], error), "Shared depth map");
    }
    for (unsigned y = 0; y < request.height; ++y)
    {
        Check(std::memcmp(depthMapped[0].data.data() + y * depthMapped[0].rowPitch,
                          depthMapped[1].data.data() + y * depthMapped[1].rowPitch, request.width * sizeof(float)) == 0,
              "Both material passes preserve shared depth bytes");
        for (unsigned x = 0; x < request.width; ++x)
        {
            const auto index = y * request.width + x;
            const auto reference = PixelReference(geometry, x, y, request, reverseOrder);
            Check(depth->IsCovered(index) == reference.covered, "Shared depth CPU coverage");
            const unsigned owners = unsigned(raster[0]->IsCovered(index)) + unsigned(raster[1]->IsCovered(index));
            Check(owners == unsigned(reference.covered),
                  "Exactly one material owns a visible pixel, including coplanar ties");
            for (unsigned i = 0; i < 2; ++i)
            {
                const bool visible = reference.covered && reference.triangle == i;
                Check(raster[i]->IsCovered(index) == visible, "Shared CPU material owner");
                const auto point = mapped[i + 5].Elements<IblBakePoint>()[index];
                const auto sample = mapped[i + 7].Elements<IblBakeSample>()[index];
                if (!visible)
                {
                    Check(point.viewTier[3] == -1 && sample.baseAverage[3] == -1, "Occluded material stays rejected");
                    continue;
                }
                const auto materialReference = PixelReference(geometry, x, y, materialRequests[i], reverseOrder);
                const auto expectedPoint = ExpectedPoint(materialReference.point, view, 1.3f, .5f, i == 1);
                const auto expected = std::bit_cast<std::array<float, 44>>(expectedPoint);
                const auto actual = std::bit_cast<std::array<float, 44>>(point);
                for (unsigned c = 0; c < actual.size(); ++c)
                {
                    Near(actual[c], expected[c], "Shared exact-pixel material", c < 3 ? 1e-3 : 1e-4);
                }
                if (index % 79 == 0)
                {
                    const auto expectedBake =
                        std::bit_cast<std::array<float, 36>>(ExpectedBake(expectedPoint, colors, table));
                    const auto actualBake = std::bit_cast<std::array<float, 36>>(sample);
                    for (unsigned c = 0; c < actualBake.size(); ++c)
                    {
                        Near(actualBake[c], expectedBake[c], "Shared exact-pixel IBL");
                    }
                }
                ++sharedDepthPixels;
                coplanarPixels += fixture == 2;
            }
        }
    }
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "Shared GPU validation: " + messages);
    for (auto readback : readbacks)
    {
        device.ReleaseReadback(readback);
    }
    for (auto readback : depthReadbacks)
    {
        device.ReleaseReadback(readback);
    }
    ++sharedDepthFrames;
    skinnedDepthFrames += fixture == 3;
}

void RunCurrentMeshFailures(RecordingChangeDevice& device, ProbePool& pool, MeshSurfaceEvaluator& evaluator)
{
    auto geometry = MakeGeometry(false, false);
    geometry.draw.modelMeshView.indexData = geometry.indices.data();
    const std::array<float, 6> lods{};
    const SurfaceView view{{0, 0, 2, 0}, 1, 900, 900};
    std::shared_ptr<const MeshSurfaceInput> input;
    std::string error;
    Check(MeshSurfaceInput::Seal(geometry.draw, view, lods, input, error), "Mesh failure input");
    for (unsigned failure = 0; failure < 3; ++failure)
    {
        Drain drain{device};
        Check(device.BeginFrame(error), "Mesh failure frame");
        EnhancedRenderGraph graph(device);
        pool.BeginFrame(failure);
        Check(graph.PrepareParallel(pool, error), "Mesh failure prefix preparation");
        std::shared_ptr<const MeshSurfaceBatch> mesh;
        Check(evaluator.Prepare(device, input, mesh, error), "Mesh failure packet preparation");
        Check(!mesh->ValidateReadback(geometry.points, error) && !mesh->IsValidated(),
              "Unrecorded geometry cannot pass plausible readback acceptance");
        const auto previous = mesh;
        if (failure == 0)
        {
            device.flushOnUpload = true;
            Check(!evaluator.Prepare(device, input, mesh, error) && mesh == previous,
                  "Mesh allocation prefix preserves the previous packet");
        }
        else if (failure == 1)
        {
            Check(device.FlushCommandList(error), "Mesh post-preparation prefix");
        }
        else
        {
            device.AbortFrame();
            Check(device.BeginFrame(error), "Mesh recording after abort");
        }
        Check(!mesh->Declare(graph, error) && !mesh->IsReadyForEvaluation(),
              "Stale mesh uploads cannot be declared in a later recording");
        device.AbortFrame();
        ++meshFailures;
    }
    {
        Drain drain{device};
        Check(device.BeginFrame(error), "Graph address reuse frame");
        std::optional<EnhancedRenderGraph> graph;
        graph.emplace(device);
        std::shared_ptr<const MeshSurfaceBatch> mesh;
        Check(evaluator.Prepare(device, input, mesh, error) && mesh->Declare(*graph, error),
              "Original graph mesh producer");
        const auto oldAddress = &*graph;
        const auto oldEpoch = graph->ResourceEpoch();
        graph.emplace(device);
        Check(&*graph == oldAddress, "Fixture reconstructs a graph at the same address");
        graph->ImportBuffer(mesh->Buffer(), RHIResourceState::Common, "Probe.ReusedOwner");
        Check(graph->ResourceEpoch() != oldEpoch && !mesh->GraphOutput(*graph).IsValid(),
              "Reused graph address/resource index cannot fabricate an old producer");
        device.AbortFrame();
        ++meshFailures;
    }
}

void RunGraphFailureCases(RecordingChangeDevice& device, ProbePool& pool, ProbeTextures& textures,
                          RenderBindingCache& bindings, RasterSurfaceCollector& collector, SurfaceEvaluator& evaluator,
                          IblBaker& baker, own::shared_owner<const Instance> instance, own::shared_owner<const Texture> cube,
                          const RasterSurfaceRequest& request,
                          std::span<const std::shared_ptr<const MeshSurfaceBatch>> sources)
{
    for (unsigned failure = 0; failure < 6; ++failure)
    {
        std::string error;
        std::shared_ptr<const RenderBindings> material;
        std::shared_ptr<const RasterSurfaceBatch> raster;
        std::shared_ptr<const SurfaceBatch> surface;
        std::shared_ptr<const IblBakeResult> baked;
        auto graph = std::make_shared<EnhancedRenderGraph>(device);
        Drain drain{device};
        Check(device.BeginFrame(error), "Failure graph begin");
        textures.BeginFrame(200 + failure);
        const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), 1, cube};
        pool.BeginFrame(failure);
        if (failure != 3)
        {
            Check(graph->PrepareParallel(pool, error), "Failure graph preparation");
        }
        Check(bindings.Prepare(device, textures, instance, evaluator.Layout(), material, error), "Failure bindings");
        Check(collector.Prepare(device, request, sources, raster, error), "Failure raster");
        Check(evaluator.PrepareGpu(device, material, raster, surface, error), "Failure surface");
        Check(baker.PrepareGpu(device, environment, surface, baked, error), "Failure bake");
        const auto acceptedSurface = surface;
        const auto acceptedBake = baked;
        if (failure == 0)
        {
            device.flushOnUpload = true;
            Check(!evaluator.PrepareGpu(device, material, raster, surface, error) && surface == acceptedSurface,
                  "Surface allocation prefix preserves previous owner");
        }
        else if (failure == 1)
        {
            device.flushOnUpload = true;
            Check(!baker.PrepareGpu(device, environment, surface, baked, error) && baked == acceptedBake,
                  "IBL allocation prefix preserves previous owner");
        }
        if (failure < 2)
        {
            Check(!raster->Declare(*graph, error) && !surface->Declare(*graph, error) && !baked->Declare(*graph, error),
                  "Allocation prefix invalidates the entire prepared chain");
        }
        else
        {
            Check(raster->Declare(*graph, error) && surface->Declare(*graph, error) && baked->Declare(*graph, error),
                  "Failure chain declaration");
            Check(graph->Compile(error), "Failure chain compile");
            if (failure == 2 || failure == 4)
            {
                Check(device.FlushCommandList(error), "Injected post-declaration prefix");
            }
            if (failure == 5)
            {
                device.AbortFrame();
                Check(device.BeginFrame(error), "Open recording after abort");
                EnhancedRenderGraph nextGraph(device);
                Check(!raster->Declare(nextGraph, error) && !surface->Declare(nextGraph, error) &&
                          !baked->Declare(nextGraph, error),
                      "Abort cannot cross recording generations");
            }
            if (failure == 4)
            {
                Check(!graph->Execute(error) && error.find("raster") != std::string::npos,
                      "Sequential callback failure is reported by Execute");
            }
            else
            {
                RHIRecordedBatchDesc description;
                description.frameId = 200 + failure;
                description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                description.lifetimeToken = graph;
                RHIRecordedBatch batch;
                Check(!graph->RecordParallel(pool, 4, description, batch, error) && !batch.IsValid(),
                      "Prefix/abort/implicit late preparation cannot publish a native batch");
                if (failure == 3)
                {
                    Check(error.find("stale") != std::string::npos || error.find("Stale") != std::string::npos,
                          "Worker callback failure retains the concrete diagnostic");
                }
            }
        }
        Check(!raster->IsReadyForEvaluation() && !surface->IsReadyForBake() && !baked->IsReady(),
              "Failed chain cannot become a ready result");
        device.AbortFrame();
        ++graphFailures;
    }
    std::string messages;
    const auto errors = device.DrainDebugMessages(messages);
    Check(errors == 0, "Graph failure GPU validation: " + messages);
}

unsigned sceneCompositionFrames{}, sceneCompositionPixels{}, sceneCompositionFailures{};
std::uint64_t sceneLookupBaked{}, sceneLookupReused{};
unsigned sceneLookupFullFrames{}, sceneLookupFullPixels{}, sceneLookupMutationFrames{};
// expanded == 5: a still view with the live split-sum lookup. Changed pixels take
// the final approximation once; a still view reuses it and never re-integrates.
unsigned sceneLookupApproximateFrames{}, sceneLookupApproximatePixels{};
std::uint64_t sceneLookupApproximated{};
unsigned sceneTextureFrames{}, sceneTexturePixels{}, sceneTextureFractionalLods{}, sceneTextureClampedLods{};
unsigned sceneGenerationFrames{}, sceneGenerationFallbacks{}, sceneGenerationRejectedRequests{}, sceneGenerationAborts{},
    sceneGenerationPendingSubmissions{};
std::uint64_t sceneGenerationStale{}, sceneGenerationCompiles{}, sceneGenerationWorkers{}, sceneGenerationPsoWorkers{};
double maxTextureFilterError{};
std::array<double, 2> sceneLookupFullMs{};

class FailingScenePipelines final : public IRenderPipelineCache
{
  public:
    explicit FailingScenePipelines(IRenderPipelineCache& cache) : cache_(cache) {}
    RHIPipelineHandle GetOrCreate(const RHIGraphicsPipelineDesc& desc, std::string& error) override
    {
        return cache_.GetOrCreate(desc, error);
    }
    RHIPipelineHandle GetOrCreateCompute(const RHIComputePipelineDesc& desc, std::string& error) override
    {
        return cache_.GetOrCreateCompute(desc, error);
    }
    RHIPipelineRequestState RequestGraphics(const RHIGraphicsPipelineDesc& desc, RHIPipelineHandle& result,
                                            std::string& error) override
    {
        if (++requests == 5)
        {
            result = {};
            error = "Injected failure after four complete Scene PSOs";
            return RHIPipelineRequestState::Failed;
        }
        const auto state = cache_.RequestGraphics(desc, result, error);
        if (state == RHIPipelineRequestState::Pending)
            --requests;
        return state;
    }
    bool InvalidatePipeline(RHIPipelineHandle handle, RHICompletionPoint after = {}) override
    {
        return cache_.InvalidatePipeline(handle, after);
    }
    std::uint32_t InvalidatePipelines(RHICompletionPoint after = {}) override
    {
        return cache_.InvalidatePipelines(after);
    }
    std::uint32_t CollectRetiredPipelines(RHICompletionPoint completed) override
    {
        return cache_.CollectRetiredPipelines(completed);
    }
    unsigned requests{};

  private:
    IRenderPipelineCache& cache_;
};

struct SceneWorkerGate
{
    struct State
    {
        std::atomic<bool> entered{}, release{};
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    job_handle job;
    explicit SceneWorkerGate(job_scheduler& scheduler)
    {
        job = scheduler.submit([held = state] {
            held->entered.store(true);
            held->entered.notify_all();
            held->release.wait(false);
        });
        state->entered.wait(false);
    }
    void Release()
    {
        state->release.store(true);
        state->release.notify_all();
    }
    ~SceneWorkerGate() { Release(); }
};

struct SceneSubmissionGate
{
    std::shared_ptr<SceneWorkerGate::State> state = std::make_shared<SceneWorkerGate::State>();
    RHISubmissionTicket ticket;
    explicit SceneSubmissionGate(RecordingChangeDevice& device)
    {
        std::string error;
        Check(GetRHISubmissionThread().Enqueue(
                  &device, "Probe.Scene.PendingSubmission",
                  [held = state](std::string&) {
                      held->entered.store(true);
                      held->entered.notify_all();
                      held->release.wait(false);
                      return true;
                  },
                  ticket, error),
              "Scene delayed native submission enqueue");
        state->entered.wait(false);
    }
    void Release()
    {
        state->release.store(true);
        state->release.notify_all();
    }
    ~SceneSubmissionGate() { Release(); }
};

own::shared_owner<const Instance> SceneReload(const own::shared_owner<const Instance>& original, double offset,
                                            bool invalid = false)
{
    Generation generationValue(*original->generation);
    generationValue.generation += 1000 + unsigned(offset * 100);
    {
        auto& program = generationValue.cooked.product.program;
        const std::string needle = ".ior = parameters.lx_p900;";
        const auto position = program.slang.find(needle);
        Check(position != std::string::npos, "Reload fixture owns the IOR expression");
        program.slang.replace(position, needle.size(), ".ior = parameters.lx_p900 + " + std::to_string(offset) + ";");
        if (invalid)
        {
            program.slang += "\nthis is an invalid Scene shader;\n";
        }
        program.semanticKey += "|scene-reload:" + std::to_string(offset) + (invalid ? ":invalid" : "");
        generationValue.cooked.boundSource = BuildBoundSource(program);
        generationValue.cooked.metadata = WriteMaterialProgramMetadata(program);
    }
    const auto generation = own::make_shared<const Generation>(std::move(generationValue));
    own::shared_owner<const Instance> result;
    std::string error;
    Check(BuildInstance(
              generation, original->description,
              [&](const experiment::AssetId& id, LXColorSpace space, std::string&) {
                  const auto found = std::ranges::find_if(original->textures, [&](const auto& texture) {
                      return texture.assetId == id && texture.colorSpace == space;
                  });
                  return found == original->textures.end() ? own::shared_owner<const Texture>{} : found->owner;
              },
              result, error),
          "Reload immutable instance " + error);
    return result;
}

void WaitSceneFailure(SceneHost& host, const EnhancedFrameContext& context,
                      const own::shared_owner<const Generation>& generation, std::uint64_t previousFailures)
{
    std::string error;
    host.RequestProgram(context, generation, error);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (host.ProgramStats().failedPreparations <= previousFailures)
    {
        host.PollPrograms(context);
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Scene failed candidate deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Check(!host.IsProgramReady(generation, RHIShaderCompiler::GetOutput()),
          "Partial/failed generation never becomes ready");
}

void WaitSceneProgram(SceneHost& host, const EnhancedFrameContext& context,
                      const own::shared_owner<const Generation>& generation)
{
    std::string error;
    Check(host.RequestProgram(context, generation, error), "Scene asynchronous request " + error);
    // A cold Vulkan driver can spend minutes preparing a Layered pipeline set.
    // This is a correctness timeout, not a product latency acceptance target.
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(RHIShaderCompiler::GetOutput() == RHIShaderBinary::SpirV ? 600 : 90);
    while (!host.IsProgramReady(generation, RHIShaderCompiler::GetOutput()))
    {
        host.PollPrograms(context);
        const auto state = host.ProgramStats();
        if (state.failedPreparations || std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Scene asynchronous preparation " + state.lastError);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void RunSceneComposition(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                         ProbeTextures& textures, ProbePool& pool,
                         const std::array<own::shared_owner<const Instance>, 2>& instances, own::shared_owner<const Texture> cube,
                         const Environment& environmentColors, const SheenTable& table, unsigned expanded = 0)
{
    std::string error;
    ProbeMeshes meshes;
    Check(meshes.Initialize(&device, error), "Scene composition mesh cache");
    thread_pool preparationThreads;
    job_scheduler controlledJobs(preparationThreads);
    if (expanded == 4)
        controlledJobs.start(1);
    SceneHost host(expanded == 4 ? controlledJobs : ce::get_job_scheduler());
    const auto psoWorkerBaseline = NativePsoWorkers(pipelines);
    FailingScenePipelines failingPipelines(pipelines);
    EnhancedGBufferPass gbuffer;
    EnhancedDeferredPass deferred;
    EnhancedFrameContext context;
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.meshCache = &meshes;
    context.width = expanded == 2 ? 1920 : expanded == 1 ? 128 : 24;
    context.height = expanded == 2 ? 1080 : expanded == 1 ? 96 : 24;
    const bool inspectPoint = expanded == 1 || expanded == 3 || expanded == 4 || expanded == 5;
    FrameCameraSnapshot camera;
    camera.view = camera.projection = math::matrix4x4::identity();
    camera.eyePosition = math::vector3(0, 0, 2);
    context.camera = &camera;
    Check(gbuffer.Initialize(context, error) && deferred.Initialize(context, error),
          "Actual Scene GBuffer/Deferred initialization " + error);
    for (const auto& instance : instances)
        WaitSceneProgram(host, context, instance->generation);
    Check(host.ProgramStats().workerExecutions == (PathFinder::IsAssetAuthoringEnabled() ? 2u : 0u),
          "Editor uses workers and cooked Player skips Scene shader compilation");
    own::shared_owner<const Instance> reloadB, reloadC, badShader, badPso, changedReload;
    std::unique_ptr<SceneWorkerGate> preparationGate;
    if (expanded == 4)
    {
        reloadB = SceneReload(instances[0], .4);
        reloadC = SceneReload(instances[0], .6);
        badShader = SceneReload(instances[0], .8, true);
        badPso = SceneReload(instances[0], .2);
        auto description = reloadC->description;
        description.parameters.push_back({900, 1.6});
        Check(
            BuildInstance(
                reloadC->generation, description,
                [&](const experiment::AssetId&, LXColorSpace, std::string&) { return reloadC->textures.front().owner; },
                changedReload, error),
            "Ready generation receives new numeric instance");
        preparationGate = std::make_unique<SceneWorkerGate>(controlledJobs);
        context.frameId = 899;
        context.sceneEpoch = 50;
        auto coldCore = MakeGeometry(false, true), coldLayered = MakeGeometry(false, true);
        coldCore.draw.geometryKey = 5001;
        coldCore.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
        coldCore.draw.modelMeshView.indexData = coldCore.indices.data();
        coldLayered.draw.geometryKey = 5002;
        coldLayered.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
        coldLayered.draw.modelMeshView.indexData = coldLayered.indices.data();
        coldCore.draw.materialGraphInstance = reloadB;
        coldCore.draw.materialGraphSlot = 11;
        coldLayered.draw.materialGraphInstance = instances[1];
        coldLayered.draw.materialGraphSlot = 12;
        std::array coldDraws{coldCore.draw, coldLayered.draw};
        SceneInputView coldView{context.frameId, context.sceneEpoch, 71, 1, context.width, context.height, camera};
        std::shared_ptr<const SceneViewInput> cold;
        const auto coldSealed = SceneViewInput::Seal(coldView, coldDraws, {}, cold, error);
        Check(coldSealed, "Cold pending slot seal " + error);
        // Target initialization must work without any native material pipeline.
        Check(device.BeginFrame(error), "Graph-only target frame begin");
        EnhancedGBufferPass graphTargets;
        EnhancedShadowPass graphShadow;
        std::vector<EnhancedDrawItem> graphCasters(coldDraws.begin(), coldDraws.end());
        const auto previousCasters = context.shadowDraws;
        context.shadowDraws = &graphCasters;
        Check(graphShadow.PrepareGraphFrame(context, error), "Graph-only cascade preparation");
        EnhancedRenderGraph clearGraph(device, RGSchedulingMode::ExplicitVersioned, RGOrderPolicy::DependencyOrder);
        graphTargets.DeclareGraphTargets(clearGraph, context);
        graphShadow.DeclareGraphTargets(clearGraph, context);
        std::array<RHIReadback, 2> cleared;
        Check(device.CreateReadback(context.width, context.height, RHIFormat::D32Float, 1, cleared[0], error) &&
                  device.CreateReadback(EnhancedShadowPass::kShadowMapSize, EnhancedShadowPass::kShadowMapSize,
                      RHIFormat::D32Float, 3, cleared[1], error), "Graph-only depth readbacks");
        const std::array clearHandles{graphTargets.GetOutputs().depth, graphShadow.GetShadowMap()};
        for (unsigned index = 0; index < clearHandles.size(); ++index)
        {
            const auto handle = clearHandles[index];
            clearGraph.AddPass("Probe.GraphOnly.Clear", {{handle, RHIResourceState::CopySource, RGAccessMode::Read}},
                [handle, target = cleared[index]](const auto& execution)
                {
                    for (unsigned layer = 0; layer < target.sliceCount; ++layer)
                    {
                        execution.encoder->CopyToReadback(target, execution.ResolveHandle(handle), layer, layer);
                    }
                }, true);
        }
        Check(clearGraph.Compile(error) && clearGraph.Execute(error) && device.EndFrame(error) &&
                  GetRHISubmissionThread().DrainSubmissions(&device, error), "Graph-only target submission");
        device.WaitForGpu();
        Check(GetRHISubmissionThread().Drain(&device, error), "Graph-only target retirement");
        for (unsigned index = 0; index < cleared.size(); ++index)
        {
            RHIReadbackImage image;
            Check(device.MapReadback(cleared[index], image, error) && image.sliceCount == (index ? 3u : 1u),
                  "Graph-only cleared depth layers");
            for (unsigned layer = 0; layer < image.sliceCount; ++layer)
            {
                float depth{};
                std::memcpy(&depth, image.data.data() + layer * image.sliceBytes, sizeof(depth));
                Check(depth == 1.f, "Each graph-only depth layer starts neutral index=" + std::to_string(index) +
                      " layer=" + std::to_string(layer) + " depth=" + std::to_string(depth));
            }
            device.ReleaseReadback(cleared[index]);
        }
        std::string clearValidation;
        Check(device.DrainDebugMessages(clearValidation) == 0,
              "Graph-only clear needs no native PSO and emits no validation errors " + clearValidation);
        context.shadowDraws = previousCasters;
        const auto requestedCold = cold;
        Check(!host.SelectReadyInput(context, cold, cold, error) && host.SelectionDeferred() &&
                  cold == requestedCold && host.ProgramStats().activeSlots == 0,
              "Cold pending material defers the frame without substituting or dropping requested draws");
    }
    const auto loadExisting = [&](const experiment::AssetId& id, LXColorSpace space, std::string&) {
        const auto found = std::ranges::find_if(instances[0]->textures, [&](const auto& texture) {
            return texture.assetId == id && texture.colorSpace == space;
        });
        return found == instances[0]->textures.end() ? own::shared_owner<const Texture>{} : found->owner;
    };
    auto maskedDescription = instances[0]->description;
    maskedDescription.parameters.push_back({902, 0.0});
    own::shared_owner<const Instance> maskedCore;
    Check(BuildInstance(instances[0]->generation, maskedDescription, loadExisting, maskedCore, error),
          "Scene masked alpha override");
    auto changedDescription = instances[0]->description;
    std::erase_if(changedDescription.parameters, [](const auto& item) { return item.id == 900; });
    changedDescription.parameters.push_back({900, 1.7});
    if (expanded == 3)
    {
        changedDescription.parameters = {{903, std::array<double, 3>{.73, .82, 0}}};
    }
    own::shared_owner<const Instance> changedCore;
    Check(BuildInstance(instances[0]->generation, changedDescription, loadExisting, changedCore, error),
          "Scene lookup parameter mutation");
    own::shared_owner<const Instance> resizedCore;
    if (expanded == 3)
    {
        const auto resized = FootprintImage(true, true);
        Check(BuildInstance(
                  instances[0]->generation, changedDescription,
                  [&](const experiment::AssetId& id, LXColorSpace space, std::string& message) {
                      return space == LXColorSpace::Data ? resized : loadExisting(id, space, message);
                  },
                  resizedCore, error),
              "Resize one image without recompiling its graph");
    }
    const std::vector<unsigned> workerModes = expanded == 4 ? std::vector<unsigned>{0, 1, 4, 0, 4, 1, 0, 4, 1, 0, 4, 1}
                                              : expanded == 1 ? std::vector<unsigned>{0, 1, 4, 1, 0, 4, 1, 4, 0, 1}
                                              : expanded == 2 ? std::vector<unsigned>{0, 4}
                                              : expanded == 3 ? std::vector<unsigned>{0, 1, 4, 0, 4, 1, 0, 4}
                                              : expanded == 5 ? std::vector<unsigned>{0, 1, 4, 0, 1, 4, 0, 1, 4, 0}
                                                              : std::vector<unsigned>{0, 1, 4};
    for (unsigned fixture = 0; fixture < (expanded ? 1u : 8u); ++fixture)
    {
        unsigned step = 0;
        own::shared_owner<const Instance> previousSelectedCore;
        for (unsigned workers : workerModes)
        {
            std::cerr << "LX_SCENE_LOOKUP_FRAME expanded=" << expanded << " fixture=" << fixture << " step=" << step
                      << " workers=" << workers << '\n';
            Drain drain{device};
            context.frameId = 900 + sceneCompositionFrames;
            context.sceneEpoch = expanded == 4 && step == 11 ? 51 : 50;
            if (expanded == 4 && step == 4)
            {
                preparationGate->Release();
                WaitSceneProgram(host, context, reloadB->generation);
                WaitSceneProgram(host, context, reloadC->generation);
            }
            if (expanded == 4 && step == 5)
                WaitSceneProgram(host, context, reloadC->generation);
            if (expanded == 4 && step == 6 && host.ProgramStats().failedPreparations == 0)
                WaitSceneFailure(host, context, badShader->generation, 0);
            if (expanded == 4 && step == 7)
            {
                context.psoManager = &failingPipelines;
                WaitSceneFailure(host, context, badPso->generation, host.ProgramStats().failedPreparations);
                Check(failingPipelines.requests == 5, "Four complete PSOs are insufficient to publish a Scene program");
                context.psoManager = &pipelines;
            }
            camera.eyePosition = expanded == 1 && step >= 2 ? math::vector3(.1f, .05f, 2) : math::vector3(0, 0, 2);
            // Scene coverage rejects back faces. Use the actual backend's
            // clockwise front winding, rather than the two-sided raster fixture.
            auto core = MakeGeometry(false, true), layered = MakeGeometry(false, true),
                 legacy = MakeGeometry(false, true);
            const auto rewrite = [&](Geometry& geometry, float dx, float scale, float z, std::size_t key) {
                if (expanded == 2)
                    scale *= .04f;
                geometry.draw.geometryKey = key;
                geometry.draw.modelMeshView.handle.generation = context.frameId;
                geometry.draw.modelMeshView.indexData = geometry.indices.data();
                for (unsigned vertex = 0; vertex < 6; ++vertex)
                {
                    const unsigned corner = vertex % 3;
                    const std::array<std::array<float, 2>, 3> triangle{{{-.75f, -.75f}, {.75f, -.75f}, {0, .75f}}};
                    geometry.points[vertex].position = {triangle[corner][0] * scale + dx, triangle[corner][1] * scale,
                                                        z, 0};
                    geometry.points[vertex].uvLod = {.1f, .2f, 0, 0};
                    if (expanded == 3)
                    {
                        const float uvScale = step >= 6 ? 128.f : 1.f;
                        geometry.points[vertex].uvLod = {(geometry.points[vertex].position[0] * .9f + .3f) * uvScale,
                                                         (geometry.points[vertex].position[1] * .65f + .55f) * uvScale,
                                                         0, 0};
                    }
                    const auto offset = vertex * geometry.draw.modelMeshView.vertexStride;
                    std::memcpy(geometry.vertices.data() + offset, geometry.points[vertex].position.data(), 12);
                    std::memcpy(geometry.vertices.data() + offset +
                                    assets::OffsetOf(geometry.draw.modelMeshView.vertexAttributeMask,
                                                     assets::VertexAttribute::Uv0),
                                geometry.points[vertex].uvLod.data(), 8);
                }
                geometry.draw.coverage.flags = EnhancedMaterialCoverage::Enabled;
            };
            rewrite(core, -.2f, .9f, .3f, 5001);
            if (expanded == 4 && step == 2)
                rewrite(core, -.15f, .9f, .3f, 5001);
            const bool coplanar = fixture == 3 || fixture == 4;
            rewrite(layered, coplanar ? -.2f : .25f, .9f, coplanar ? .3f : .4f, 5002);
            rewrite(legacy, -.4f, .5f, .1f, 5003);
            // Distinct model cache identity for the actual legacy uploader.
            Check(Uuid::TryParse("33333333-3333-8333-8333-333333333333", legacy.draw.modelMeshView.handle.meshId) &&
                      legacy.draw.modelMeshView.IsComplete(),
                  "Actual legacy uploader receives a valid typed UUIDv8 view");
            core.draw.materialGraphInstance = fixture == 5 ? maskedCore : instances[0];
            if (expanded == 4)
            {
                core.draw.materialGraphSlot = 11;
                layered.draw.materialGraphSlot = 12;
                core.draw.materialGraphInstance = step <= 2                ? (step ? reloadB : instances[0])
                                                  : step <= 5 || step == 8 ? reloadC
                                                  : step == 7              ? badPso
                                                  : step == 10             ? changedReload
                                                                           : badShader;
                if (step == 1 || step == 2)
                    core.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
                if (step >= 3 && step <= 5 || step == 8 || step == 10)
                    core.draw.coverage.flags |= EnhancedMaterialCoverage::DoubleSided;
                if (step == 9)
                    core.draw.coverage.flags |= EnhancedMaterialCoverage::Blended;
            }
            if (expanded == 1 && step >= 4)
                core.draw.materialGraphInstance = changedCore;
            if (expanded == 3 && step >= 3)
                core.draw.materialGraphInstance = step >= 5 ? resizedCore : changedCore;
            if (expanded == 1 && step >= 5)
            {
                const auto uvOffset =
                    assets::OffsetOf(core.draw.modelMeshView.vertexAttributeMask, assets::VertexAttribute::Uv0);
                for (unsigned vertex = 0; vertex < 6; ++vertex)
                {
                    core.points[vertex].uvLod[0] = .7f;
                    std::memcpy(core.vertices.data() + vertex * core.draw.modelMeshView.vertexStride + uvOffset,
                                core.points[vertex].uvLod.data(), 8);
                }
            }
            if (expanded == 1 && step >= 9)
            {
                core.draw.worldMatrix.m[3][2] = .02f;
                layered.draw.worldMatrix.m[3][2] = .02f;
            }
            if (fixture == 5 || fixture == 6)
                core.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
            layered.draw.materialGraphInstance = instances[1];
            legacy.draw.baseColorFactor = math::color(.3f, .6f, .2f, fixture == 1 ? 0.f : 1.f);
            if (fixture == 1 || fixture == 2)
                legacy.draw.coverage.flags |= EnhancedMaterialCoverage::Masked;
            std::vector<EnhancedDrawItem> oldDraws{legacy.draw};
            context.draws = &oldDraws;
            const auto lightDirection = Unit({.35, -.2, .8});
            EnhancedLight light;
            light.direction =
                math::vector4(float(-lightDirection[0]), float(-lightDirection[1]), float(-lightDirection[2]), 0);
            light.color = math::color(1, 1, 1, 1);
            std::vector<EnhancedLight> lights{light};
            context.lights = &lights;
            SceneInputView view{context.frameId, context.sceneEpoch, 71, 1, context.width, context.height, camera};
            std::array<EnhancedDrawItem, 2> graphDraws{core.draw, layered.draw};
            if (fixture == 4)
                std::swap(graphDraws[0], graphDraws[1]);
            std::shared_ptr<const SceneViewInput> input;
            Check(SceneViewInput::Seal(view, graphDraws, {}, input, error), "Scene composition seal " + error);
            own::shared_owner<const Instance> selectedCore;
            if (expanded == 4)
            {
                auto conflicting = graphDraws;
                conflicting[1].materialGraphSlot = 11;
                std::shared_ptr<const SceneViewInput> conflict;
                Check(SceneViewInput::Seal(view, conflicting, {}, conflict, error),
                      "Conflicting slot input remains sealed");
                auto retained = input;
                Check(!host.SelectReadyInput(context, conflict, retained, error) && retained == input,
                      "Conflicting Material identities preserve the selected view");
                auto staleView = view;
                ++staleView.frameId;
                std::shared_ptr<const SceneViewInput> staleEmpty;
                Check(SceneViewInput::Seal(staleView, {}, {}, staleEmpty, error) &&
                          !host.SelectReadyInput(context, staleEmpty, retained, error) && retained == input,
                      "A stale empty view cannot clear current Material slots");
                const auto requestedInput = input;
                const bool accepted = host.SelectReadyInput(context, input, input, error);
                if (!accepted)
                {
                    const bool pending = step >= 1 && step <= 3;
                    Check(input == requestedInput && host.SelectionDeferred() == pending && !error.empty(),
                          "Pending or failed request cannot substitute a previously submitted material step=" + std::to_string(step) +
                          " selectionDeferred=" + std::to_string(host.SelectionDeferred()) + " error=" + error);
                    Check(step == 1 || step == 2 || step == 3 || step == 6 || step == 7 || step == 9 || step == 11,
                          "Only known pending/failed fixtures defer recording");
                    ++sceneGenerationRejectedRequests;
                    ++step;
                    continue;
                }
                const auto coreInput =
                    std::ranges::find_if(input->Draws(), [](const auto& draw) { return draw.materialSlot == 11; });
                Check(coreInput != input->Draws().end() &&
                          material_graph_test::SamePinnedObject(coreInput->material, core.draw.materialGraphInstance) &&
                          coreInput->coverage.flags == core.draw.coverage.flags,
                      "Every accepted pass uses the exact requested material and coverage");
                selectedCore = coreInput->material;
                if (step == 8)
                {
                    Check(device.BeginFrame(error), "Unsubmitted replacement begin");
                    textures.BeginFrame(context.frameId);
                    Check(host.PrepareResidency(context, input, error) &&
                              host.Prepare(context, input, {}, {}, {}, {}, {}, error, 1),
                          "Prepare replacement before abort");
                    Check(!host.PublishSubmittedCache(context.frameId, {UINT64_MAX}, error),
                          "Prepared replacement is not a submission");
                    device.AbortFrame();
                    Check(host.ProgramStats().activeSlots == 2 && host.ProgramStats().retainedRecordings == 0,
                          "Aborted replacement retains submitted slots and releases recording owners");
                    ++sceneGenerationAborts;
                    error.clear();
                }
            }
            Check(device.BeginFrame(error), "Scene composition begin");
            textures.BeginFrame(context.frameId);
            meshes.BeginFrame(static_cast<std::uint32_t>(context.frameId));
            const auto environment = textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
            Check(environment.IsValid(), "Scene reference environment");
            Check(gbuffer.PrepareFrame(context, error) && deferred.PrepareFrame(context, error),
                  "Actual Scene pass preparation " + error);
            const auto resident = host.PrepareResidency(context, input, error);
            Check(resident, "Scene texture residency " + error);
            auto graph = std::make_shared<EnhancedRenderGraph>(device);
            if (workers)
            {
                pool.BeginFrame(static_cast<std::uint32_t>(context.frameId));
                Check(graph->PrepareParallel(pool, error), "Scene native prefix " + error);
            }
            const auto hostPrepared =
                host.Prepare(context, input, fixture == 7 ? RHITextureHandle{} : environment.handle, {}, {}, {},
                             SceneHostBudget{.lookupApproximate = expanded == 5},
                             error, expanded == 1 && step >= 7 ? 2 : 1);
            Check(hostPrepared, "Scene host prepare " + error);
            for (unsigned failure = 0; failure < (expanded ? 4u : 3u); ++failure)
            {
                auto wrong = context;
                SceneHostBudget budget;
                if (failure == 0)
                    wrong.frameId++;
                if (failure == 1)
                    budget.pixels = 8;
                if (failure == 2)
                    budget.draws = expanded == 4 ? 0 : 1;
                if (failure == 3)
                    budget.lookupBytes = 1;
                Check(!host.Prepare(wrong, input, environment.handle, {}, {}, {}, budget, error, 1) && host.HasDraws(),
                      "Failed Scene host candidate retains complete accepted input");
                ++sceneCompositionFailures;
            }
            gbuffer.Declare(*graph, context);
            const auto outputs = gbuffer.GetOutputs();
            host.DeclareGBuffer(*graph, outputs);
            RGTextureDesc aoDesc;
            aoDesc.width = context.width;
            aoDesc.height = context.height;
            aoDesc.format = RHIFormat::RG16Float;
            aoDesc.allowRenderTarget = true;
            const float occlusion = fixture == 1 ? .5f : 1.f;
            aoDesc.clearColor[0] = occlusion;
            const auto ao = graph->CreateTexture(aoDesc);
            graph->AddPass("Probe.Scene.AO", {{ao, RHIResourceState::RenderTarget}},
                           [&device, ao, occlusion](const auto& execution) {
                               const auto handle = execution.ResolveHandle(ao);
                               const auto target = device.CreateRenderTargets({&handle, 1}, nullptr);
                               Check(target.IsValid(), "Scene AO target");
                               const float value[]{occlusion, 0, 0, 0};
                               execution.encoder->BindRenderTargets(target);
                               execution.encoder->ClearRenderTargets(target, value);
                           });
            deferred.SetInputs(outputs);
            deferred.SetAmbientOcclusion(ao);
            deferred.Declare(*graph, context);
            std::array<RHIReadback, 5> readbacks;
            const std::array formats{RHIFormat::RGBA16Float, RHIFormat::RGBA16Float, RHIFormat::R32Uint,
                                     RHIFormat::D32Float, RHIFormat::D32Float};
            for (unsigned i = 0; i < 5; ++i)
                Check(device.CreateReadback(context.width, context.height, formats[i], 1, readbacks[i], error),
                      "Scene readback allocation");
            const auto copy = [&](unsigned index, RGHandle handle) {
                graph->AddPass(
                    "Probe.Scene.Readback", {{handle, RHIResourceState::CopySource}},
                    [readback = readbacks[index], handle](const auto& execution) {
                        execution.encoder->CopyToReadback(readback, execution.ResolveHandle(handle));
                    },
                    true);
            };
            copy(0, deferred.GetOutput());
            copy(3, outputs.depth);
#ifndef LX_PROBE_VULKAN
            Microsoft::WRL::ComPtr<ID3D12QueryHeap> timer;
            RHIReadback timerReadback;
            std::uint64_t timerFrequency{};
            if (expanded == 2)
            {
                const D3D12_QUERY_HEAP_DESC desc{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2, 0};
                Check(SUCCEEDED(device.GetDevice()->CreateQueryHeap(&desc, IID_PPV_ARGS(&timer))) &&
                          SUCCEEDED(device.GetCommandQueue()->GetTimestampFrequency(&timerFrequency)) &&
                          device.CreateBufferReadback(16, timerReadback, error),
                      "Scene lookup GPU timer allocation");
                graph->AddPass(
                    "Probe.Scene.LookupTimerStart", {},
                    [timer](const auto& execution) {
                        static_cast<DX12Encoder&>(*execution.encoder)
                            .GetCommandList()
                            ->EndQuery(timer.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
                    },
                    true);
            }
#endif
            host.DeclareColor(*graph, outputs, deferred.GetOutput(), ao, {});
            std::array<RHIReadback, 11> pointReadbacks;
            RHIReadback sampleReadback;
            if (inspectPoint)
            {
                const auto lookup = host.LookupFrame();
                for (unsigned field = 0; field < pointReadbacks.size(); ++field)
                {
                    Check(device.CreateReadback(context.width, context.height, RHIFormat::RGBA32Float, 1,
                                                pointReadbacks[field], error),
                          "Scene lookup point readback");
                    const auto resource = graph->FindImportedTexture(lookup->Inputs()[field]);
                    graph->AddPass(
                        "Probe.Scene.LookupPointReadback", {{resource, RHIResourceState::CopySource}},
                        [readback = pointReadbacks[field], resource](const auto& execution) {
                            execution.encoder->CopyToReadback(readback, execution.ResolveHandle(resource));
                        },
                        true);
                    graph->AddPass(
                        "Probe.Scene.LookupPointRestore", {{resource, RHIResourceState::ShaderResource}},
                        [](const auto&) {}, true);
                }
                Check(device.CreateBufferReadback(std::uint64_t(context.width) * context.height * sizeof(IblBakeSample),
                                                  sampleReadback, error),
                      "Scene lookup sample readback");
                graph->AddPass(
                    "Probe.Scene.LookupSampleReadback", {{lookup->GraphSamples(*graph), RHIResourceState::CopySource}},
                    [sampleReadback, buffer = lookup->Samples()](const auto& execution) {
                        execution.encoder->CopyBufferToReadback(sampleReadback, buffer);
                    },
                    true);
                graph->AddPass(
                    "Probe.Scene.LookupSampleRestore",
                    {{lookup->GraphSamples(*graph), RHIResourceState::ShaderResource}}, [](const auto&) {}, true);
            }
#ifndef LX_PROBE_VULKAN
            if (expanded == 2)
                graph->AddPass(
                    "Probe.Scene.LookupTimerEnd", {},
                    [timer, timerReadback, &device](const auto& execution) {
                        auto* list = static_cast<DX12Encoder&>(*execution.encoder).GetCommandList();
                        list->EndQuery(timer.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
                        list->ResolveQueryData(timer.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2,
                                               device.Resolve(timerReadback.buffer), 0);
                    },
                    true);
#endif
            RHIReadback lookupReadback;
            Check(device.CreateBufferReadback(sizeof(SceneLookupStats), lookupReadback, error),
                  "Scene lookup stats allocation");
            const auto statistics = host.GraphLookupStatistics(*graph);
            graph->AddPass(
                "Probe.Scene.LookupStatistics", {{statistics, RHIResourceState::CopySource}},
                [lookupReadback, buffer = host.LookupStatistics()](const auto& execution) {
                    execution.encoder->CopyBufferToReadback(lookupReadback, buffer);
                },
                true);
            for (bool otherGraph : {false, true})
            {
                EnhancedRenderGraph unrelated(device);
                bool rejected = false;
                try
                {
                    host.DeclareColor(otherGraph ? unrelated : *graph, outputs, deferred.GetOutput(), ao, {});
                }
                catch (const std::runtime_error&)
                {
                    rejected = true;
                }
                Check(rejected, "Scene color rejects duplicate/cross-graph declaration");
                ++sceneCompositionFailures;
            }
            copy(1, deferred.GetOutput());
            copy(2, outputs.bitmask);
            copy(4, outputs.depth);
            Check(!host.PublishSubmittedCache(context.frameId, {UINT64_MAX}, error),
                  "Unrecorded cache cannot be published");
            Check(graph->Compile(error), "Scene composition graph compile " + error);
            RHICompletionPoint graphCompletion;
            RHISubmissionTicket graphTicket;
            std::unique_ptr<SceneSubmissionGate> submissionGate;
            if (workers)
            {
                graph->SetParallelRecordCostThreshold(0);
                RHIRecordedBatchDesc description;
                description.frameId = context.frameId;
                description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                description.lifetimeToken = graph;
                RHIRecordedBatch batch;
                Check(graph->RecordParallel(pool, workers, description, batch, error),
                      "Scene native worker recording " + error);
                if ((expanded == 4 && step == 8) || (expanded == 0 && fixture == 0 && step == 1))
                    submissionGate = std::make_unique<SceneSubmissionGate>(device);
                Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), graphTicket,
                                                                    error),
                      "Scene native worker submission " + error);
                graphCompletion = graphTicket.GetRecordedBatch()->GetCompletionPoint();
            }
            else
                Check(graph->Execute(error), "Scene sequential graph execution " + error);
            if (expanded == 4 && step == 5)
            {
                auto newerDraws = graphDraws;
                newerDraws[0].materialGraphInstance = badShader;
                std::shared_ptr<const SceneViewInput> newer;
                Check(SceneViewInput::Seal(view, newerDraws, {}, newer, error) &&
                          !host.SelectReadyInput(context, newer, newer, error),
                      "An unready newer request supersedes publication without selecting a fallback");
            }
            // Worker submission already happened above. Sequential recording
            // must not be authorized by a previous frame's completed fence.
            if (!workers)
                Check(!host.PublishSubmittedCache(context.frameId, {UINT64_MAX}, error),
                      "Recorded but unsubmitted cache is rejected");
            Check(device.EndFrame(error), "Scene frame enqueue " + error);
            if (!workers)
                graphCompletion = {device.GetLastSignaledFenceValue()};
            if (submissionGate)
            {
                const auto publications = host.ProgramStats().publications;
                Check(!graphTicket.IsComplete() &&
                          host.PublishSubmittedCache(context.frameId, graphCompletion, error, graphTicket) &&
                          host.ProgramStats().publications == publications,
                      "Pending native submission queues ownership without replacing a submitted Material slot");
                submissionGate->Release();
                if (expanded == 4) ++sceneGenerationPendingSubmissions;
            }
            Check(GetRHISubmissionThread().DrainSubmissions(&device, error),
                  "Scene native submission completion " + error);
            // Keep the first fixture's completed publication deferred until
            // the next immediate frame. This path does not select/compile
            // materials between frames, so no intermediate poll can hide it.
            if (submissionGate && expanded == 4)
                host.PollPrograms(context);
            Check(!host.PublishSubmittedCache(context.frameId + 1, {device.GetLastSignaledFenceValue()}, error) &&
                      !host.PublishSubmittedCache(context.frameId, {}, error),
                  "Invalid cache publication is rejected");
            if (!submissionGate)
                Check(host.PublishSubmittedCache(context.frameId, graphCompletion, error, graphTicket),
                      "Scene lookup publication follows full graph submission " + error);
            Check(!host.PublishSubmittedCache(context.frameId, graphCompletion, error, graphTicket),
                  "Duplicate cache publication is rejected");
            if (expanded == 0 && fixture == 0 && step == 2)
            {
                host.PollPrograms(context);
                Check(host.ProgramStats().failedSubmissions == 0,
                      "Completed newer publication drains older deferred frames in FIFO order");
                std::cout << "LX_SCENE_PUBLICATION_ORDER_OK deferred=1 immediate=1 failed=0\n";
            }
            device.WaitForGpu();
            Check(GetRHISubmissionThread().Drain(&device, error), "Scene submission retirement");
#ifdef LX_PROBE_VULKAN
            Check(pool.GetEncoderUnimplementedCount() == 0 && device.GetEncoderUnimplementedCount() == 0,
                  "Scene recording must not drop native Vulkan operations");
            if (expanded == 2 && !workers)
            {
                const auto descriptors = device.GetDescriptorRecyclerStats();
                Check(descriptors.peakRecordingSets > 256 && descriptors.allocationFailures == 0,
                      "Full viewport grows descriptor pools without losing recorded sets");
            }
#endif
            RHIReadbackImage lookupMapped;
            Check(device.MapReadback(lookupReadback, lookupMapped, error), "Scene lookup stats map");
            SceneLookupStats stats;
            std::memcpy(&stats, lookupMapped.data.data(), sizeof(stats));
            Check(stats.rejected == 0 && stats.baked + stats.approximated + stats.reused == stats.visible,
                  "Scene lookup validates every visible point");
            const bool approximate = expanded == 5;
            Check(approximate || stats.approximated == 0,
                  "A reference lookup integrates every changed pixel at reference counts");
            if (approximate)
            {
                Check(stats.baked == 0, "The split-sum lookup never runs the reference integration");
                if (step == 0)
                    Check(stats.approximated == stats.visible && stats.reused == 0,
                          "A cold approximate view takes split-sum samples only");
                else
                    Check(stats.approximated == 0 && stats.reused == stats.visible,
                          "A still approximate view reuses its split-sum samples step=" + std::to_string(step));
                sceneLookupApproximated += stats.approximated;
                ++sceneLookupApproximateFrames;
            }
            const bool warm = approximate     ? false
                              : expanded == 4 ? step > 0 && step != 2 && step != 3 && step != 11 &&
                                                  material_graph_test::SamePinnedObject(selectedCore, previousSelectedCore)
                              : expanded == 1 ? step == 1 || step == 3 || step == 6 || step == 8
                              : expanded == 3 ? step == 1 || step == 2 || step == 4 || step == 7
                                              : step > 0;
            if (warm)
                Check(stats.baked == 0 && stats.reused == stats.visible,
                      "Unchanged Scene pixels reuse exact IBL for all worker modes");
            if (expanded == 1 && (step == 0 || step == 2 || step == 7 || step == 9))
                Check(stats.baked == stats.visible && stats.reused == 0,
                      "Camera/world/environment changes invalidate exact lookup");
            if (expanded == 1 && (step == 4 || step == 5))
                Check(stats.baked > 0 && stats.reused > 0, "Material parameter/UV changes update only affected pixels");
            if (expanded == 3 && (step == 3 || step == 5))
                Check(stats.baked > 0 && stats.reused > 0, "Per-image Vector/extent updates affect only owning pixels");
            if (expanded == 4 && selectedCore && previousSelectedCore &&
                !material_graph_test::SamePinnedObject(selectedCore, previousSelectedCore) && step != 2 && step != 3)
                Check(stats.baked > 0 && stats.reused > 0,
                      "Generation or instance replacement preserves the other slot's exact lookup");
            if (expanded == 4) previousSelectedCore = selectedCore;
            if (expanded == 3 && (step == 0 || step == 6))
                Check(stats.baked == stats.visible && stats.reused == 0,
                      "Independent mip footprints update every changed pixel");
            sceneLookupBaked += stats.baked;
            sceneLookupReused += stats.reused;
            device.ReleaseReadback(lookupReadback);
            if (expanded == 2)
            {
#ifndef LX_PROBE_VULKAN
                RHIReadbackImage mappedTimer;
                Check(device.MapReadback(timerReadback, mappedTimer, error), "Scene lookup GPU timer map");
                std::array<std::uint64_t, 2> ticks;
                std::memcpy(ticks.data(), mappedTimer.data.data(), sizeof(ticks));
                Check(ticks[1] >= ticks[0] && timerFrequency > 0, "Scene lookup GPU timestamps are ordered");
                sceneLookupFullMs[step] = double(ticks[1] - ticks[0]) * 1000 / timerFrequency;
                device.ReleaseReadback(timerReadback);
#endif
                ++sceneLookupFullFrames;
                sceneLookupFullPixels += stats.visible;
            }
            if (expanded == 1)
                ++sceneLookupMutationFrames;
            if (expanded == 3)
                ++sceneTextureFrames;
            std::array<RHIReadbackImage, 5> mapped;
            std::array<RHIReadbackImage, 11> mappedPoint;
            RHIReadbackImage mappedSample;
            if (inspectPoint)
            {
                for (unsigned field = 0; field < pointReadbacks.size(); ++field)
                    Check(device.MapReadback(pointReadbacks[field], mappedPoint[field], error),
                          "Scene lookup point map");
                Check(device.MapReadback(sampleReadback, mappedSample, error), "Scene lookup sample map");
            }
            for (unsigned i = 0; i < 5; ++i)
                Check(device.MapReadback(readbacks[i], mapped[i], error), "Scene readback map");
            std::array<unsigned, 2> visible{};
            unsigned legacyPixels{};
            for (unsigned y = 0; y < context.height; ++y)
            {
                Check(std::memcmp(mapped[3].data.data() + y * mapped[3].rowPitch,
                                  mapped[4].data.data() + y * mapped[4].rowPitch, context.width * sizeof(float)) == 0,
                      "Color composition preserves shared GBuffer depth bytes");
                for (unsigned x = 0; x < context.width; ++x)
                {
                    const auto owner =
                        reinterpret_cast<const std::uint32_t*>(mapped[2].data.data() + y * mapped[2].rowPitch)[x];
                    if ((owner & 0x80000000u) == 0)
                    {
                        for (unsigned c = 0; c < 4; ++c)
                            Check(mapped[0].At(x, y, c) == mapped[1].At(x, y, c),
                                  "Legacy/background color remains intact");
                        legacyPixels += owner == 0xABCDu;
                        continue;
                    }
                    Check(owner == 0x80000001u || owner == 0x80000002u, "Exact Scene material owner");
                    const unsigned tier = expanded == 4
                                              ? (input->Draws()[owner - 0x80000001u].materialSlot == 12 ? 1 : 0)
                                          : fixture == 4 ? (owner == 0x80000001u ? 1 : 0)
                                                         : owner - 0x80000001u;
                    ++visible[tier];
                    SurfacePoint spatial;
                    spatial.position = {
                        float((x + .5) / context.width * 2 - 1), float(1 - (y + .5) / context.height * 2),
                        (tier == 0 || coplanar ? .3f : .4f) + (expanded == 1 && step >= 9 ? .02f : 0.f), 0};
                    spatial.uvLod = {expanded == 1 && tier == 0 && step >= 5 ? .7f : .1f, .2f, 0, 0};
                    const float ior = expanded == 4 && tier == 0                ? (material_graph_test::SamePinnedObject(selectedCore, changedReload) ? 2.2f
                                                                                   : material_graph_test::SamePinnedObject(selectedCore, reloadC)    ? 1.9f
                                                                                   : material_graph_test::SamePinnedObject(selectedCore, reloadB)    ? 1.7f
                                                                                                                : 1.3f)
                                      : expanded == 1 && tier == 0 && step >= 4 ? 1.7f
                                                                                : 1.3f;
                    auto point = ExpectedPoint(spatial, input->Surface(), ior, .5f, tier == 1);
                    point.metalIorLevelAo[3] = occlusion;
                    if (expanded == 3)
                    {
                        const double scale = step >= 6 ? 128 : 1;
                        const auto a = ImageExtent(false), b = ImageExtent(true, tier == 0 && step >= 5);
                        const double lodA = ImageLod(a, context.width, context.height, scale);
                        const double lodB = ImageLod(b, context.width, context.height, scale);
                        const double u = (spatial.position[0] * .9 + .3) * scale;
                        const double v = (spatial.position[1] * .65 + .55) * scale;
                        const auto color = ReferenceImage(false, false, u, v, lodA);
                        const auto rough = ReferenceImage(true, tier == 0 && step >= 5, u, v, lodB);
                        const auto metal = ReferenceImage(false, false, tier == 0 && step >= 3 ? .73 : .23,
                                                          tier == 0 && step >= 3 ? .82 : .37, 0, true);
                        for (unsigned c = 0; c < 3; ++c)
                            point.baseAlpha[c] = color[c];
                        point.normalRoughness[3] = rough[3];
                        point.metalIorLevelAo[0] = metal[3];
                        Check(std::abs(lodA - lodB) > .1, "Images have distinct dimension-derived LODs");
                        if (step < 6)
                        {
                            Check(lodA > 0 && lodA < a.levels - 1 && std::abs(lodA - std::round(lodA)) > .1 &&
                                      lodB > 0 && lodB < b.levels - 1 && std::abs(lodB - std::round(lodB)) > .1,
                                  "Both images exercise fractional mip filtering");
                            ++sceneTextureFractionalLods;
                        }
                        else
                        {
                            Check(lodA == a.levels - 1 && lodB == b.levels - 1,
                                  "Each image clamps to its own last mip");
                            ++sceneTextureClampedLods;
                        }
                        ++sceneTexturePixels;
                    }
                    if (inspectPoint)
                    {
                        const auto reference = std::bit_cast<std::array<float, 44>>(point);
                        std::array<float, 44> captured;
                        for (unsigned field = 0; field < 11; ++field)
                            for (unsigned component = 0; component < 4; ++component)
                            {
                                const auto index = field * 4 + component;
                                captured[index] = mappedPoint[field].At(x, y, component);
                                // Hardware linear/trilinear UNORM filtering is
                                // a separate transport gate from 1e-4 BRDF math.
                                const bool filtered = index < 3 || (expanded == 3 && index == 7);
                                if (expanded == 3 && filtered)
                                    maxTextureFilterError = std::max(
                                        maxTextureFilterError, std::abs(double(captured[index]) - reference[index]));
                                Near(captured[index], reference[index],
                                     "Scene captured material and raster frame field=" + std::to_string(index),
                                     filtered ? 1e-3 : 1e-4);
                            }
                        // Hardware rasterization snaps vertices to its subpixel grid.
                        // Keep that transport check separate from the independent BRDF
                        // reference, especially for the zero-roughness specular peak.
                        point = std::bit_cast<IblBakePoint>(captured);
                    }
                    // A split-sum sample is an approximation, not the reference.
                    // Only its finiteness is checked here; its distance from the
                    // reference is a separate material-similarity measurement.
                    if (inspectPoint &&
                        mappedSample.Elements<IblBakeSample>()[y * context.width + x].baseAverage[3] == 1.f)
                    {
                        Check(expanded == 5, "Only the approximate lookup stores split-sum samples");
                        for (unsigned c = 0; c < 3; ++c)
                            Check(std::isfinite(mapped[1].At(x, y, c)), "Split-sum Scene lighting is finite");
                        ++sceneLookupApproximatePixels;
                        ++sceneCompositionPixels;
                        continue;
                    }
                    const auto bake = ExpectedBake(point, environmentColors, table);
                    if (inspectPoint)
                    {
                        const auto expectedSamples = std::bit_cast<std::array<float, 36>>(bake);
                        const auto* actualSamples = mappedSample.Elements<IblBakeSample>() + y * context.width + x;
                        const auto actual = std::bit_cast<std::array<float, 36>>(*actualSamples);
                        for (unsigned component = 0; component < actual.size(); ++component)
                            Near(actual[component], expectedSamples[component], "Scene exact cached IBL physics");
                    }
                    const auto ambient = fixture == 7 ? Vector{} : Ambient(point, bake, table);
                    auto probe = std::bit_cast<MaterialProbe::Reference::ProbeInput>(point);
                    probe.options = {};
                    const auto material = Evaluate(probe, tier ? 0x7ff : 0x7f);
                    const auto direct = Rgb(MaterialProbe::Reference::Reference(
                        material, Unit(Rgb4(point.viewTier)), tier ? 0x7ff : 0x7f, table).fields[14]);
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        const double expected = ambient[c] + direct[c];
                        if (inspectPoint &&
                            std::abs(mapped[1].At(x, y, c) - expected) > .001 * std::max(1.0, std::abs(expected)))
                        {
                            const auto* pointFloats = reinterpret_cast<const float*>(&point);
                            for (unsigned field = 0; field < 11; ++field)
                            {
                                std::cerr << "POINT field=" << field;
                                for (unsigned component = 0; component < 4; ++component)
                                    std::cerr << " " << mappedPoint[field].At(x, y, component) << "/"
                                              << pointFloats[field * 4 + component];
                                std::cerr << '\n';
                            }
                            const auto* sampleFloats = reinterpret_cast<const float*>(&bake);
                            const auto* actualFloats =
                                reinterpret_cast<const float*>(mappedSample.data.data()) + (y * context.width + x) * 36;
                            for (unsigned field = 0; field < 9; ++field)
                            {
                                std::cerr << "SAMPLE field=" << field;
                                for (unsigned component = 0; component < 4; ++component)
                                    std::cerr << " " << actualFloats[field * 4 + component] << "/"
                                              << sampleFloats[field * 4 + component];
                                std::cerr << '\n';
                            }
                            std::cerr << "LIGHT ambient=" << ambient[c] << " direct=" << direct[c] << '\n';
                        }
                        Check(std::isfinite(mapped[1].At(x, y, c)) && std::abs(mapped[1].At(x, y, c) - expected) <=
                                                                          .001 * std::max(1.0, std::abs(expected)),
                              "Scene HDR half output matches independent Core/Layered lighting reference tier=" +
                                  std::to_string(tier) + " expanded=" + std::to_string(expanded) + " step=" +
                                  std::to_string(step) + " pixel=" + std::to_string(x) + "," + std::to_string(y) + "," +
                                  std::to_string(c) + " actual=" + std::to_string(mapped[1].At(x, y, c)) +
                                  " expected=" + std::to_string(expected));
                        ++gpuComponents;
                    }
                    ++sceneCompositionPixels;
                }
            }
            if (inspectPoint)
            {
                for (auto& readback : pointReadbacks)
                    device.ReleaseReadback(readback);
                device.ReleaseReadback(sampleReadback);
            }
            std::string validation;
            const auto validationCount = device.DrainDebugMessages(validation);
            Check(validationCount == 0, "Scene composition GPU validation " + validation);
            Check(visible[fixture == 4 || fixture == 5 || (expanded == 4 && step == 11) ? 1 : 0] > 10,
                  "Core/Layered Scene pixels are visible fixture=" + std::to_string(fixture) +
                      " workers=" + std::to_string(workers) + " core=" + std::to_string(visible[0]) +
                      " layered=" + std::to_string(visible[1]) + " legacy=" + std::to_string(legacyPixels) +
                      " depthCenter=" + std::to_string(mapped[3].At(12, 12, 0)));
            if (!coplanar)
                Check(visible[1] > 10, "Far material remains independently visible");
            if (expanded == 4 && step == 11)
                Check(visible[0] == 0, "Previous epoch's Core material cannot appear in the new Scene");
            if (coplanar)
                Check(visible[fixture == 4 ? 0 : 1] == 0, "Coplanar color cannot overwrite the depth winner");
            if (fixture == 5)
                Check(visible[0] == 0, "LX masked alpha discards without stealing depth or color");
            Check(fixture == 1 ? legacyPixels == 0 : legacyPixels > 5,
                  "Actual legacy opaque/masked shared-depth policy");
            for (auto& readback : readbacks)
                device.ReleaseReadback(readback);
            graph.reset();
            ++sceneCompositionFrames;
            if (expanded == 4)
                ++sceneGenerationFrames;
            ++step;
        }
    }
    if (expanded == 4)
    {
        const auto stats = host.ProgramStats();
        Check(stats.compileSubmissions == 6 && stats.workerExecutions == 6 && stats.failedPreparations == 2,
              "Each exact generation compiles once on a worker, including two memoized failures");
        Check(stats.stalePublications == 1 && stats.failedSubmissions == 0 && stats.activeSlots == 0 &&
                  stats.retainedRecordings == 0,
              "Stale submission cannot replace an active slot and completion releases owners");
        Check(sceneGenerationFallbacks == 0 && sceneGenerationRejectedRequests == 7,
              "All seven pending/failed requests are rejected with zero material fallbacks");
        sceneGenerationCompiles = stats.compileSubmissions;
        sceneGenerationWorkers = stats.workerExecutions;
        sceneGenerationStale = stats.stalePublications;
        sceneGenerationPsoWorkers = NativePsoWorkers(pipelines) - psoWorkerBaseline;
        Check(sceneGenerationPsoWorkers > 0, "Native Scene PSO creation executes on pool workers");
    }
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        Check(host.ProgramStats().compileSubmissions == 0 && host.ProgramStats().workerExecutions == 0,
              "Cooked Scene runs and retires without a compiler job");
    }
    host.ShutdownAfterIdle();
    gbuffer.Shutdown();
    deferred.Shutdown();
    meshes.Shutdown();
}

#include "material_scene_subsurface_tests.inl"
#include "material_scene_refraction_tests.inl"
#include "material_scene_volume_tests.inl"
#include "csm_vulkan_inflight.inl"
#include "material_scene_shadow_decal_tests.inl"
#include "material_scene_decal_tests.inl"
#include "material_forward_blend_tests.inl"
#include "material_forward_transport_tests.inl"

void RunIssueAdmission(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                       const std::shared_ptr<const Generation>& generation)
{
    EnhancedFrameContext context{};
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    std::string error;
    {
        SceneHost host;
        for (unsigned revision = 0; revision < 80; ++revision)
        {
            auto invalid = std::make_shared<Generation>(*generation);
            invalid->cooked.product.program.volume = true;
            invalid->cooked.product.program.slang.clear();
            Check(!host.RequestProgram(context, invalid, error), "Invalid volume revision is rejected");
            const auto failed = host.ProgramStats().failedPreparations;
            Check(!host.RequestProgram(context, invalid, error)
                      && host.ProgramStats().failedPreparations == failed,
                  "Repeated failed revision preserves its memoized error");
        }
        Check(host.RequestProgram(context, generation, error), "Corrected generation enters after 80 failures");
        const auto failures = host.ProgramStats().failedPreparations;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
        while (!host.IsProgramReady(generation, RHIShaderCompiler::GetOutput()))
        {
            host.PollPrograms(context);
            Check(host.ProgramStats().failedPreparations == failures,
                  "Corrected generation introduces no new preparation failure");
            Check(std::chrono::steady_clock::now() < deadline, "Corrected generation completes preparation");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        Check(host.IsProgramReady(generation, RHIShaderCompiler::GetOutput()),
              "Corrected generation prepares without recreating host");
        host.ShutdownAfterIdle();
    }
    {
        SceneHost host;
        for (unsigned request = 0; request < 64; ++request)
        {
            Check(host.RequestProgram(context, std::make_shared<Generation>(*generation), error),
                  "Pending request fits admission budget");
        }
        const auto overflow = std::make_shared<Generation>(*generation);
        Check(!host.RequestProgram(context, overflow, error) && host.ProgramStats().compileSubmissions > 0,
              "Admission overflow starts existing jobs instead of deadlocking before PollPrograms");
        host.ShutdownAfterIdle();
    }
    std::cout << "ISSUE_ADMISSION_OK failedRevisions=80 pendingRequests=64 overflowProgress=1\n";
}

void RunIssueResourceLifetime(RecordingChangeDevice& device, ProbeRoots& roots, ProbePipelines& pipelines,
                             ProbeTextures& textures, ProbePool& pool)
{
    std::string error;
    EnhancedFrameContext context{};
    context.resources = &device;
    context.rootSignatures = &roots;
    context.psoManager = &pipelines;
    context.textureCache = &textures;
    context.width = context.height = 64;
    device.trackOwnedResources = true;
    {
        RHIBufferHandle buffer;
        RHIBufferDesc desc{};
        desc.bytes = 64;
        Check(device.CreateBuffer(desc, buffer, error), "Graph state test buffer");
        RHIResourceState state = RHIResourceState::Common;
        for (unsigned decision = 0; decision < 4; ++decision)
        {
            const auto initial = state;
            const auto finalState = decision == 3 ? RHIResourceState::ShaderResource : RHIResourceState::CopyDest;
            Check(device.BeginFrame(error), "Graph state recording opens");
            EnhancedRenderGraph graph(device, RGSchedulingMode::ExplicitSingleWriter);
            if (decision == 3)
            {
                pool.BeginFrame(0);
                Check(graph.PrepareParallel(pool, error), "Graph state parallel preparation");
            }
            const auto imported = graph.ImportBuffer(buffer, state, "StateAcceptance", &state);
            graph.AddPass("StateAcceptance", {{imported, finalState,
                decision == 3 ? RGAccessMode::Read : RGAccessMode::Write}},
                [decision](const EnhancedRenderGraph::ExecuteContext&)
                {
                    if (decision == 1)
                    {
                        throw std::runtime_error("Injected graph recording failure");
                    }
                }, true);
            Check(graph.Compile(error) && state == initial,
                  "Compile never commits planned external resource state decision=" + std::to_string(decision)
                      + " error=" + error);
            if (decision == 0)
            {
                device.AbortFrame();
            }
            else if (decision == 1)
            {
                Check(!graph.Execute(error), "Callback failure is reported");
                device.AbortFrame();
            }
            else
            {
                if (decision == 3)
                {
                    graph.SetParallelRecordCostThreshold(0);
                    RHIRecordedBatch batch;
                    RHIRecordedBatchDesc description;
                    description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                    Check(graph.RecordParallel(pool, 2, description, batch, error) && state == initial,
                          "Parallel recording alone never commits external state");
                    RHISubmissionTicket ticket;
                    Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error)
                              && state == finalState, "Parallel queue acceptance commits external state");
                }
                else
                {
                    Check(graph.Execute(error) && state == initial,
                          "Recording alone never commits external resource state");
                }
                Check(device.EndFrame(error) && state == finalState,
                      "Queue acceptance commits external resource state");
                Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Graph state submission");
                device.WaitForGpu();
                Check(GetRHISubmissionThread().Drain(&device, error), "Graph state retirement");
            }
            if (decision < 2)
            {
                Check(state == RHIResourceState::Common, "Aborted graph preserves initial external state");
            }
        }
        device.ReleaseBuffer(buffer);
    }
    for (unsigned fail = 2; fail <= 4; ++fail)
    {
        EnhancedSSGIPass pass;
        device.textureAttempt = 0;
        device.failTextureAt = fail;
        Check(!pass.PrepareFrame(context, error) && device.ownedTextures.empty(),
              "SSGI partial allocation is rolled back");
        device.failTextureAt = 0;
        Check(pass.PrepareFrame(context, error) && device.ownedTextures.size() == 4,
              "SSGI retries the complete four-texture set");
        pass.Shutdown();
        Check(device.ownedTextures.empty(), "SSGI shutdown releases owned textures");
    }
    for (unsigned fail = 2; fail <= 3; ++fail)
    {
        EnhancedVolumetricFogPass pass;
        device.textureAttempt = 0;
        device.failTextureAt = fail;
        Check(!pass.Initialize(context, error) && device.ownedTextures.empty(),
              "Fog partial allocation is rolled back");
        device.failTextureAt = 0;
        for (unsigned cycle = 0; cycle < 3; ++cycle)
        {
            Check(pass.Initialize(context, error) && device.ownedTextures.size() == 3,
                  "Fog on/off creates exactly three owned volumes");
            pass.Shutdown();
            Check(device.ownedTextures.empty(), "Fog off releases all owned volumes");
        }
    }
    {
        EnhancedForwardPass pass;
        auto opaqueOnly = context;
        opaqueOnly.forwardLightingConsumer = false;
        Check(pass.PrepareFrame(opaqueOnly, error) && device.ownedBuffers.empty(),
              "Opaque graph-only view does not allocate Forward light-culling buffers");
        EnhancedRenderGraph graph(device);
        pass.Declare(graph, opaqueOnly);
        Check(device.ownedBuffers.empty(), "Opaque graph-only view does not declare Forward culling work");
        pass.Shutdown();
    }
    for (unsigned fail = 1; fail <= 2; ++fail)
    {
        EnhancedForwardPass pass;
        device.bufferAttempt = 0;
        device.failBufferAt = fail;
        Check(!pass.PrepareFrame(context, error) && device.ownedBuffers.empty(),
              "Forward partial buffer allocation is rolled back");
        device.failBufferAt = 0;
        Check(pass.PrepareFrame(context, error) && device.ownedBuffers.size() == 2,
              "Forward retries the complete tile-buffer pair");
        Check(device.BeginFrame(error), "Forward growth recording opens");
        context.width = context.height = 128;
        Check(pass.PrepareFrame(context, error) && device.ownedBuffers.size() == 4,
              "Forward growth retains previous buffers until recording decision");
        device.AbortFrame();
        Check(device.ownedBuffers.size() == 2, "Aborted unused recording releases retired pair");
        pass.Shutdown();
        Check(device.ownedBuffers.empty(), "Forward shutdown releases tile buffers");
        context.width = context.height = 64;
    }
    {
        PassResourceRetirement retirement;
        Check(retirement.Attach(device), "Retirement attaches to owning device");
        auto* listener = device.lastListener;
        RHIBufferHandle buffer{};
        RHIBufferDesc desc{};
        desc.bytes = 64;
        Check(device.CreateBuffer(desc, buffer, error), "Retirement test buffer");
        Check(device.BeginFrame(error), "Retirement test recording");
        const auto recording = device.GetCurrentUploadRecordingId();
        unsigned acceptedMetadata{}, rejectedMetadata{};
        retirement.WatchRecording([&] { ++rejectedMetadata; }, [&] { ++acceptedMetadata; });
        retirement.WatchRecording([&] { ++rejectedMetadata; }, [&] { ++acceptedMetadata; });
        retirement.Retire(buffer);
        listener->OnUploadSubmitted(recording, RHICompletionPoint{100});
        Check(acceptedMetadata == 0 && rejectedMetadata == 0,
              "Submission reservation cannot publish temporal metadata");
        listener->OnUploadAccepted(recording, RHICompletionPoint{100});
        Check(acceptedMetadata == 2 && rejectedMetadata == 0,
              "Acceptance commits every metadata transaction in its recording");
        listener->OnUploadCompleted(99);
        Check(device.ownedBuffers.size() == 1, "In-flight retirement waits for exact completion");
        listener->OnUploadCompleted(100);
        Check(device.ownedBuffers.empty(), "Completed retirement releases its resource");
        device.AbortFrame();
        listener->OnUploadSubmitted(999, RHICompletionPoint{200});
        listener->OnUploadAccepted(999, RHICompletionPoint{200});
        Check(device.CreateBuffer(desc, buffer, error), "Rejected recording retirement buffer");
        Check(device.BeginFrame(error), "Rejected retirement recording");
        const auto rejectedRecording = device.GetCurrentUploadRecordingId();
        retirement.WatchRecording([&] { ++rejectedMetadata; }, [&] { ++acceptedMetadata; });
        retirement.WatchRecording([&] { ++rejectedMetadata; }, [&] { ++acceptedMetadata; });
        retirement.Retire(buffer);
        listener->OnUploadSubmissionRejected(rejectedRecording, RHICompletionPoint{201});
        Check(acceptedMetadata == 2 && rejectedMetadata == 2,
              "Rejection rolls back initialization and temporal metadata together");
        listener->OnUploadCompleted(199);
        Check(device.ownedBuffers.size() == 1, "Rejected recording still preserves prior in-flight use");
        listener->OnUploadCompleted(200);
        Check(device.ownedBuffers.empty(), "Prior completion releases rejected-recording retirement");
        device.AbortFrame();
        retirement.ClearAfterIdle();
    }
    device.trackOwnedResources = false;
    {
        const auto cooked = []
        {
            assets::CookedEnvironment value;
            value.cubeSize = value.brdfSize = 1;
            for (unsigned image = 0; image < value.images.size(); ++image)
            {
                value.images[image] = TextureImage::Allocate(EnhancedIBLGenerator::CookedImageFormat(image),
                    1, 1, image < 3 ? 6 : 1, 1, image < 3);
            }
            for (auto& importance : value.importance)
            {
                importance = TextureImage::Allocate(RHIFormat::RGBA32Float, 1, 1, 1, 1);
            }
            value.source = TextureImage::Allocate(RHIFormat::RGBA32Float, 1, 1, 1, 1);
            return value;
        };
        EnhancedIBLGenerator generator;
        Check(generator.Initialize(context, error), "IBL transaction initialization");
        Check(device.BeginFrame(error), "IBL initial cooked recording");
        Check(generator.InstallCooked(context, cooked(), error), "IBL initial cooked preparation: " + error);
        Check(device.EndFrame(error) && GetRHISubmissionThread().DrainSubmissions(&device, error),
              "IBL initial environment accepted");
        device.WaitForGpu();
        const auto previousCube = generator.GetCubeMap();
        const auto previousGeneration = generator.GetGeneration();
        Check(device.BeginFrame(error), "IBL replacement recording");
        Check(generator.InstallCooked(context, cooked(), error), "IBL replacement preparation");
        bool prepared = true;
        generator.WatchPreparationRejected([&prepared] { prepared = false; });
        const auto capturePath = std::filesystem::path("Build/Verification/GitHubIssueAudit20261008/aborted-ibl.ceibl");
        Check(generator.QueueCookedCapture(capturePath, {}, error) && generator.HasPendingCookedCapture(),
              "IBL replacement capture is recorded");
        device.AbortFrame();
        Check(!prepared && generator.GetCubeMap() == previousCube
                  && generator.GetGeneration() == previousGeneration && !generator.HasPendingCookedCapture(),
              "Aborted IBL restores previous environment and discards unsubmitted capture");
        Check(!std::filesystem::exists(capturePath), "Aborted IBL capture is never published");
        Check(device.BeginFrame(error), "IBL partial preparation recording");
        auto invalid = cooked();
        invalid.images[3] = {};
        Check(!generator.InstallCooked(context, std::move(invalid), error)
                  && generator.GetCubeMap() == previousCube && generator.GetGeneration() == previousGeneration,
              "Partial IBL preparation preserves previous environment");
        device.AbortFrame();
        generator.Shutdown();
    }
    std::string validation;
    Check(device.DrainDebugMessages(validation) == 0, "Resource lifetime GPU validation: " + validation);
    std::cout << "ISSUE_RESOURCE_LIFETIME_OK ssgiFailurePoints=3 fogFailurePoints=2 fogCycles=6 forwardFailurePoints=2 validation=0\n";
}

void Run(const std::filesystem::path& root, std::string_view mode = {}, const std::filesystem::path& cookedRoot = {})
{
#ifdef LX_PROBE_VULKAN
    RHIShaderCompiler::ScopedOutput nativeOutput(RHIShaderBinary::SpirV);
#endif
    auto& preparationJobs = ce::get_job_scheduler();
    preparationJobs.start(4);
    struct PreparationDrain
    {
        job_scheduler& jobs;
        ~PreparationDrain() { jobs.shutdown(); }
    } preparationDrain{preparationJobs};
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->CacheRoot = root / "Build/Obj/MaterialProductProbe/SceneCache";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const std::array products{Product(root, false), Product(root, true)};
    const std::array footprintProducts{Product(root, false, true), Product(root, true, true)};
    const auto table = LoadSheenTable(root / "Tools/blender/fixtures/principled-layered-5.1.1/sheen-ltc.csv");
    const Environment environmentColors{{{2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}}};
    const auto image = Image(), cube = Cube(environmentColors);
    const auto footprintColor = FootprintImage(false), footprintData = FootprintImage(true);
    experiment::AssetId graphId;
    Check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graphId.value), "Graph GUID");
    GenerationStore store;
    std::string error;
    std::array<own::shared_owner<const Instance>, 2> instances;
    std::array<own::shared_owner<const Instance>, 2> footprintInstances;
    for (unsigned tier = 0; tier < 2; ++tier)
    {
        const auto generation = store.Load(
            graphId,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[tier], WriteMaterialProgramMetadata(products[tier].program),
                          BuildBoundSource(products[tier].program)};
                return true;
            },
            true, error);
        Check(generation && BuildInstance(
                                generation, {graphId, {}, {}},
                                [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; },
                                instances[tier], error),
              "Raster material instance " + error);
        const auto footprintGeneration = store.Load(
            graphId,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {footprintProducts[tier], WriteMaterialProgramMetadata(footprintProducts[tier].program),
                          BuildBoundSource(footprintProducts[tier].program)};
                return true;
            },
            true, error);
        Check(footprintGeneration && BuildInstance(
                                         footprintGeneration, {graphId, {}, {}},
                                         [&](const experiment::AssetId&, LXColorSpace space, std::string&) {
                                             return space == LXColorSpace::Data ? footprintData : footprintColor;
                                         },
                                         footprintInstances[tier], error),
              "Multi-image material instance " + error);
    }
    RecordingChangeDevice device;
    RunSceneInputFailures(instances);
    Check(device.Initialize(64, 64, error), "Native device " + error);
#ifdef LX_PROBE_VULKAN
    ProbePipelines pipelines;
    pipelines.Initialize(device.GetDevice());
    device.SetPipelineCache(&pipelines);
    Check(device.IsValidationEnabled(), "Native Vulkan validation layer is required");
    auto& roots = pipelines;
#else
    ProbeRoots roots;
    ProbePipelines pipelines;
#endif
    ProbeTextures textures;
    thread_pool recordingThreads;
    job_scheduler recordingJobs(recordingThreads);
    recordingJobs.start(4);
    ProbePool pool(recordingJobs);
#ifdef LX_PROBE_VULKAN
    Check(textures.Initialize(&device, error),
#else
    Check(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error) &&
              textures.Initialize(&device, error),
#endif
          "Native caches " + error);
    Check(pool.Initialize(device, 4, ProbeDevice::kFrameCount, error), "Native command pool " + error);
    if (mode == "--issue-resource-lifetime")
    {
        RunIssueResourceLifetime(device, roots, pipelines, textures, pool);
        ShutdownNative(device, roots, pipelines, textures, pool);
        return;
    }
    if (mode == "--issue-admission")
    {
        RunIssueAdmission(device, roots, pipelines, instances[0]->generation);
        ShutdownNative(device, roots, pipelines, textures, pool);
        return;
    }
    if (mode == "--forward-blend")
    {
        RunForwardBlend(root, device, roots, pipelines, textures, pool, instances, cube);
        ShutdownNative(device, roots, pipelines, textures, pool);
        return;
    }
    if (mode == "--forward-transport" || mode == "--rg5-mixed")
    {
        RunForwardTransport(root, device, roots, pipelines, textures, pool, image, cube, mode == "--rg5-mixed");
        ShutdownNative(device, roots, pipelines, textures, pool);
        return;
    }
    if (mode == "--cooked-scene")
    {
        namespace ck = experiment::cooked;
        std::ifstream manifest(cookedRoot / "Derived/asset-manifest.cemf", std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(manifest), {}};
        std::vector<ck::AssetManifestIssue> issues;
        const auto catalog = ck::CookedAssetCatalog::Load(
            {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}, cookedRoot, issues);
        Check(issues.empty() && catalog.CountOfKind(ck::CookedAssetKind::MaterialProgram) == 2,
              "Automatic cooker produces both complete Scene generations");
        const ck::LooseArtifactByteSource loose(cookedRoot);
        Pak::BuildOptions packOptions;
        packOptions.encrypt = true;
        packOptions.compress = false;
        const auto pakFile = cookedRoot.parent_path() / "scene-cooked.pak";
        Pak::Builder builder(pakFile, packOptions);
        builder.addFile("Assets/Derived/asset-manifest.cemf", cookedRoot / "Derived/asset-manifest.cemf");
        for (const auto& entry : catalog.Entries())
        {
            builder.addFile("Assets/" + entry.artifactPath, cookedRoot / entry.artifactPath);
        }
        builder.finish();
        auto archive = std::make_shared<const Pak::Archive>(pakFile, Pak::OpenOptions{.key = builder.key()});
        const ck::PakAudioClipByteSource mounted(archive);
        for (unsigned tier = 0; tier < 2; ++tier)
        {
            const char* idText = tier ? "33333333-3333-4333-8333-333333333333" : "11111111-1111-4111-8111-111111111111";
            experiment::AssetId id;
            Check(Uuid::TryParse(idText, id.value), "Cooked fixture GUID");
            CookedProgram plain, packed;
            Check(catalog.OpenMaterialProgram(id, loose, {}, plain, error) &&
                      catalog.OpenMaterialProgram(id, mounted, {}, packed, error) &&
                      plain.product.program.semanticKey == packed.product.program.semanticKey &&
                      std::ranges::equal(plain.product.shaders, packed.product.shaders,
                                         [](const auto& a, const auto& b) {
                                             return a.backend == b.backend && a.entryPoint == b.entryPoint &&
                                                    a.bytecode == b.bytecode;
                                         }),
                  "Encrypted PAK and loose complete Scene bytecode match " + error);
            SceneShaderSet accepted;
            Check(LoadSceneShaders(packed.product, RHIShaderBinary::Dxil, accepted, error),
                  "Load complete cooked Scene shaders " + error);
            SceneShaderSet spirv;
            Check(LoadSceneShaders(packed.product, RHIShaderBinary::SpirV, spirv, error) &&
                      spirv.vertex.bytecode.Size() > 0 && spirv.color.bytecode.Size() > 0,
                  "Load complete cooked Vulkan stage set " + error);
            const auto acceptedSize = accepted.color.bytecode.Size();
            auto incomplete = packed.product;
            incomplete.targets.pop_back();
            Check(!LoadSceneShaders(incomplete, RHIShaderBinary::Dxil, accepted, error) &&
                      accepted.color.bytecode.Size() == acceptedSize,
                  "Incomplete cooked stage set preserves accepted shaders");
            Generation generationValue;
            generationValue.assetId = id;
            generationValue.generation = 1;
            generationValue.cooked = std::move(packed);
            const auto generation = own::make_shared<const Generation>(std::move(generationValue));
            Check(BuildInstance(
                      generation, {id, {}, {}},
                      [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; }, instances[tier],
                      error),
                  "Cooked Scene instance " + error);
        }
        paths->AssetAuthoringEnabled = false;
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table);
        std::string validation;
        Check(device.DrainDebugMessages(validation) == 0, "Cooked Scene validation " + validation);
        ShutdownNative(device, roots, pipelines, textures, pool);
        std::cout << "LX_MATERIAL_COOKED_SCENE_OK frames=" << sceneCompositionFrames << " checks=" << checks
                  << " gpuComponents=" << gpuComponents << " sceneCompiles=0 validation=0 pak=encrypted\n";
        return;
    }
    if (mode == "--scene-only" || mode == "--scene-full-only")
    {
        if (mode != "--scene-full-only")
        {
            RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 4);
            RunSceneComposition(device, roots, pipelines, textures, pool, footprintInstances, cube, environmentColors,
                                table, 3);
            RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table);
            RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 1);
            RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 5);
        }
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 2);
        ShutdownNative(device, roots, pipelines, textures, pool);
        std::cout << "LX_MATERIAL_SCENE_INTEGRATION_OK frames=" << sceneCompositionFrames
                  << " generationFrames=" << sceneGenerationFrames << " checks=" << checks
                  << " gpuComponents=" << gpuComponents << " stale=" << sceneGenerationStale
                  << " fallbacks=" << sceneGenerationFallbacks << " aborts=" << sceneGenerationAborts << '\n';
        return;
    }
    if (!mode.empty() && mode != "--rg5-reference")
    {
        if (mode == "--refraction-only")
        {
            RunSceneRefraction(device, roots, pipelines, textures, pool, root, image, cube, instances[0]);
        }
        else if (mode == "--volume-only")
        {
            RunSceneVolume(device, roots, pipelines, textures, pool, root, image, cube, instances[0]);
        }
        else if (mode == "--shadow-decal-only")
        {
#ifdef LX_PROBE_VULKAN
            RunCsmVulkanInFlight(device);
#endif
            RunSceneShadow(device, roots, pipelines, textures, pool, root, image);
            RunSceneDecal(device, roots, pipelines, textures, pool, root, image, cube, mode == "--rg5-decal");
        }
        else if (mode == "--decal-only" || mode == "--rg5-decal")
        {
            RunSceneDecal(device, roots, pipelines, textures, pool, root, image, cube, mode == "--rg5-decal");
        }
        else
        {
            RunSceneSubsurface(device, roots, pipelines, textures, pool, root, image, cube, table);
        }
        ShutdownNative(device, roots, pipelines, textures, pool);
        return;
    }
    {
        const auto before = textures.GetCachedCount();
        std::array<own::shared_owner<const Texture>, 3> abortedImages{Image(), Image(), Image()};
        std::array<RHITextureHandle, 3> handles;
        Check(device.BeginFrame(error), "Texture abort regression begin");
        textures.BeginFrame(800);
        for (unsigned i = 0; i < handles.size(); ++i)
        {
            const auto uploaded = textures.GetOrUpload((abortedImages[i] ? &*abortedImages[i].borrow() : nullptr), abortedImages[i] ? abortedImages[i]->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
            Check(uploaded.IsValid(), "Fresh texture upload before abort");
            handles[i] = uploaded.handle;
        }
        Check(textures.GetCachedCount() == before + 3, "Fresh texture cache entries");
        device.AbortFrame();
        Check(textures.GetCachedCount() == before, "Abort erases every transaction without invalid iteration");
        for (const auto handle : handles)
            Check(device.DescribeTexture(handle).width == 0, "Aborted texture handle is released");
        Check(device.BeginFrame(error), "Texture retry regression begin");
        textures.BeginFrame(801);
        for (const auto& owner : abortedImages)
            Check(textures.GetOrUpload((owner ? &*owner.borrow() : nullptr), owner ? owner->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error).IsValid(), "Aborted texture retries on fresh recording");
        Check(device.EndFrame(error), "Texture retry submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Texture retry drain");
        device.WaitForGpu();
    }
    if (mode != "--rg5-reference")
    {
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 4);
        RunSceneComposition(device, roots, pipelines, textures, pool, footprintInstances, cube, environmentColors, table, 3);
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table);
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 1);
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 5);
        RunSceneComposition(device, roots, pipelines, textures, pool, instances, cube, environmentColors, table, 2);
    }
    RHIShaderBlob transform, vertex, pixel, sharedPixel, resolve, bake;
    for (auto backend : {RHIShaderBinary::SpirV, RHIShaderBinary::Dxil})
    {
        RHIShaderCompiler::ScopedOutput output(backend);
        const auto path = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader";
        const auto compile = [&](const std::string& file, const char* entry, const char* profile,
                                 RHIShaderBlob& shader) {
            RHIShaderCompileOptions options;
            options.fineDerivatives = file.find("MaterialGraphRasterSurface.slang") != std::string::npos;
            const bool compiled = RHIShaderCompiler::CompileFile(file, entry, profile, shader, error, options);
            Check(compiled, std::string(entry) + ": " + error);
        };
        compile((path / "MaterialGraphMeshSurface.slang").string(), "LXTransformMesh", "cs_6_0", transform);
        const auto file = (path / "MaterialGraphRasterSurface.slang").string();
        compile(file, "LXRasterVS", "vs_6_0", vertex);
        compile(file, "LXRasterPS", "ps_6_0", pixel);
        compile(file, "LXRasterSharedPS", "ps_6_0", sharedPixel);
        compile(file, "LXResolveRaster", "cs_6_0", resolve);
        compile((path / "PrincipledIblBake.slang").string(), "CSMain", "cs_6_0", bake);
        if (backend == RHIShaderBinary::SpirV)
        {
            RHIShaderBlob rejected;
            const bool compiled = RHIShaderCompiler::CompileFile(file, "LXRasterPS", "ps_6_0", rejected, error);
            Check(!compiled && error.find("spvDerivativeControl") != std::string::npos,
                  "Derivative capability is explicit and cannot leak through compiler cache");
        }
    }
    MeshSurfaceEvaluator meshEvaluator;
    std::array<RasterSurfaceCollector, 2> collectors;
    std::array<SurfaceEvaluator, 2> evaluators;
    IblBaker baker;
    RenderBindingCache bindings;
    Check(meshEvaluator.Initialize(device, roots, pipelines, transform, error), "Mesh evaluator " + error);
    Check(baker.Initialize(device, roots, pipelines, bake, error), "Baker " + error);
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(collectors[i].Initialize(device, roots, pipelines, vertex, pixel, resolve, sharedPixel, i == 0, error),
              "Raster initialization " + error);
        Check(evaluators[i].Initialize(device, roots, pipelines, products[i], RHIShaderBinary::Dxil, {}, error),
              "Surface evaluator " + error);
    }
    RHIShaderBlob empty;
    Check(!collectors[0].Initialize(device, roots, pipelines, empty, pixel, resolve, sharedPixel, true, error),
          "Failed raster initialization preserves pipeline");
    std::array<unsigned, 2> culledCounts{};
    unsigned coveredPixels{}, backgroundPixels{}, farPixels{}, fractionalLods{};
    for (unsigned frame = 0; frame < 7; ++frame)
    {
        const unsigned tier = frame % 2;
        const bool culling = frame == 4 || frame == 5;
        auto geometry = MakeGeometry(frame == 2, frame == 3 || frame == 5);
        // Vector storage is stable after return; the fixed index array needs rebinding.
        geometry.draw.modelMeshView.indexData = geometry.indices.data();
        const bool split = frame == 1;
        auto secondDraw = geometry.draw;
        if (split)
        {
            geometry.draw.modelMeshView.indexCount = 3;
            secondDraw.modelMeshView.indexCount = 3;
            secondDraw.modelMeshView.indexData += 3;
        }
        SurfaceView view{{.1f, .2f, 2, 0}, 1, frame + 1, frame + 1};
        std::array<float, 6> lods{};
        std::shared_ptr<const MeshSurfaceInput> input;
        std::shared_ptr<const MeshSurfaceBatch> mesh;
        std::shared_ptr<const MeshSurfaceInput> secondInput;
        std::shared_ptr<const MeshSurfaceBatch> secondMesh;
        std::shared_ptr<const RasterSurfaceBatch> raster;
        std::shared_ptr<const SurfaceBatch> surface;
        std::shared_ptr<const IblBakeResult> baked;
        Check(MeshSurfaceInput::Seal(geometry.draw, view, lods, input, error), "Raster geometry seal " + error);
        if (split)
        {
            Check(MeshSurfaceInput::Seal(secondDraw, view, lods, secondInput, error), "Second chunk seal " + error);
        }
        RasterSurfaceRequest request;
        request.width = frame == 6 ? 64 : 32;
        request.height = frame == 6 ? 64 : 24;
        request.viewProjection = math::matrix4x4::identity();
        request.viewProjection.m[2][2] = .4f;
        request.viewProjection.m[2][3] = .5f;
        request.viewProjection.m[3][2] = .2f;
        request.texture = {{}, {}, 4, 2, 2, frame == 0 ? 0.f : frame == 2 ? 2.9f : 4.f};
        const unsigned count = request.width * request.height;
        std::array<RHIReadback, 6> readbacks;
        Check(device.CreateBufferReadback(6 * sizeof(SurfacePoint), readbacks[0], error), "Mesh readback");
        Check(device.CreateBufferReadback(count * sizeof(SurfacePoint), readbacks[1], error), "Raster readback");
        Check(device.CreateBufferReadback(count * sizeof(IblBakePoint), readbacks[2], error), "Graph readback");
        Check(device.CreateBufferReadback(count * sizeof(IblBakeSample), readbacks[3], error), "Bake readback");
        Check(device.CreateReadback(request.width, request.height, RHIFormat::RGBA32Float, 1, readbacks[4], error),
              "Derivative readback");
        if (split)
        {
            Check(device.CreateBufferReadback(6 * sizeof(SurfacePoint), readbacks[5], error), "Second chunk readback");
        }
        Drain drain{device};
        Check(device.BeginFrame(error), "Raster begin");
        textures.BeginFrame(frame);
        std::shared_ptr<const RenderBindings> materialBindings;
        Check(bindings.Prepare(device, textures, instances[tier], evaluators[tier].Layout(), materialBindings, error),
              "Raster material bindings " + error);
        const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), cube ? cube->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error), 1, cube};
        auto& encoder = device.GetImmediateEncoder();
        if (frame == 0)
        {
            const auto texture = textures.GetOrUpload((image ? &*image.borrow() : nullptr), image ? image->NonRehydratableImage() : own::shared_owner<const Texture::CodecImage>{}, error);
            const RHITransition transitions[]{
                {texture.handle, RHIResourceState::PixelShaderResource, RHIResourceState::ShaderResource},
                {environment.cube.handle, RHIResourceState::PixelShaderResource, RHIResourceState::ShaderResource}};
            encoder.ResourceBarriers({transitions});
        }
        Check(meshEvaluator.Record(device, input, mesh, error), "World frame " + error);
        std::vector<std::shared_ptr<const MeshSurfaceBatch>> sources{mesh};
        if (split)
        {
            Check(meshEvaluator.Record(device, secondInput, secondMesh, error), "Second chunk world frame " + error);
            sources.push_back(secondMesh);
        }
        auto& collector = collectors[culling ? 1 : 0];
        Check(collector.Prepare(device, request, sources, raster, error), "Raster prepare " + error);
        const auto accepted = raster;
        auto badRequest = request;
        badRequest.width = 4097;
        Check(!collector.Prepare(device, badRequest, {&mesh, 1}, raster, error) && raster == accepted,
              "Viewport budget preserves batch");
        badRequest = request;
        badRequest.texture.mipLevels = 33;
        Check(!collector.Prepare(device, badRequest, {&mesh, 1}, raster, error) && raster == accepted,
              "Footprint mismatch preserves batch");
        const std::array duplicate{mesh, mesh};
        Check(!collector.Prepare(device, request, duplicate, raster, error) && raster == accepted,
              "Duplicate source rejects inconsistent graph state tracking");
        badRequest = request;
        std::memset(&badRequest.viewProjection, 0, sizeof(badRequest.viewProjection));
        Check(!collector.Prepare(device, badRequest, {&mesh, 1}, raster, error) && raster == accepted,
              "Singular camera preserves accepted batch");
        Check(!evaluators[tier].RecordGpu(device, materialBindings, raster, surface, error) && !surface,
              "An unrecorded raster cannot become a material input");
        {
            EnhancedRenderGraph graph(device);
            Check(raster->Declare(graph, error), "Raster declare " + error);
            Check(!raster->Declare(graph, error), "Duplicate declaration rejected");
            Check(graph.Compile(error) && graph.Execute(error), "Graph capture/resolve " + error);
            Check(raster->IsReadyForEvaluation(), "Three graph passes recorded");
        }
        Check(evaluators[tier].RecordGpu(device, materialBindings, raster, surface, error), "Raster -> graph " + error);
        Check(baker.RecordGpu(device, environment, surface, baked, error), "Graph -> IBL " + error);
        Check(surface->Matches(*instances[tier], *raster), "Exact raster source identity");
        const RHIBufferHandle buffers[]{mesh->Buffer(), raster->Buffer(), surface->Buffer(), baked->Buffer()};
        for (unsigned i = 0; i < 4; ++i)
        {
            const RHIBufferTransition before{buffers[i], RHIResourceState::ShaderResource,
                                             RHIResourceState::CopySource};
            encoder.ResourceBarriers({{}, {&before, 1}});
            encoder.CopyBufferToReadback(readbacks[i], buffers[i]);
            const RHIBufferTransition after{buffers[i], RHIResourceState::CopySource, RHIResourceState::ShaderResource};
            encoder.ResourceBarriers({{}, {&after, 1}});
        }
        if (split)
        {
            const RHIBufferTransition before{secondMesh->Buffer(), RHIResourceState::ShaderResource,
                                             RHIResourceState::CopySource};
            encoder.ResourceBarriers({{}, {&before, 1}});
            encoder.CopyBufferToReadback(readbacks[5], secondMesh->Buffer());
            const RHIBufferTransition after{secondMesh->Buffer(), RHIResourceState::CopySource,
                                            RHIResourceState::ShaderResource};
            encoder.ResourceBarriers({{}, {&after, 1}});
        }
        const RHITransition derivative{raster->Derivatives(), RHIResourceState::ShaderResource,
                                       RHIResourceState::CopySource};
        encoder.ResourceBarriers({{&derivative, 1}});
        encoder.CopyToReadback(readbacks[4], raster->Derivatives());
        Check(device.EndFrame(error), "Raster submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Raster native submit");
        device.WaitForGpu();
        std::array<RHIReadbackImage, 6> mapped;
        for (unsigned i = 0; i < (split ? 6u : 5u); ++i)
        {
            Check(device.MapReadback(readbacks[i], mapped[i], error), "Raster map");
        }
        const auto actualMesh = mapped[0].Elements<SurfacePoint>();
        const auto actual = mapped[1].Elements<SurfacePoint>();
        const auto actualMaterial = mapped[2].Elements<IblBakePoint>();
        const auto actualBake = mapped[3].Elements<IblBakeSample>();
        Check(!raster->ValidateReadback({actual, count}, error), "Raster acceptance requires accepted vertices");
        Check(!surface->ValidateReadback({actualMaterial, count}, error),
              "Material acceptance requires accepted pixels");
        Check(mesh->ValidateReadback({actualMesh, 6}, error), "Mesh acceptance " + error);
        if (split)
        {
            Check(!raster->ValidateReadback({actual, count}, error), "Every chunk must be accepted first");
            Check(secondMesh->ValidateReadback({mapped[5].Elements<SurfacePoint>(), 6}, error),
                  "Second chunk acceptance");
        }
        Check(raster->ValidateReadback({actual, count}, error), "Raster acceptance " + error);
        Check(surface->ValidateReadback({actualMaterial, count}, error), "Material acceptance " + error);
        unsigned frameCovered{}, frameBackground{}, frameFar{};
        for (unsigned y = 0; y < request.height; ++y)
        {
            for (unsigned x = 0; x < request.width; ++x)
            {
                const unsigned index = y * request.width + x;
                const auto reference = PixelReference(geometry, x, y, request);
                const bool covered = raster->IsCovered(index);
                if (!culling)
                {
                    Check(covered == reference.covered,
                          "Exact raster clipping/depth coverage at " + std::to_string(x) + "," + std::to_string(y));
                }
                if (!covered)
                {
                    ++frameBackground;
                    Check(actualMaterial[index].viewTier[3] == -1 && actualBake[index].baseAverage[3] == -1,
                          "Background remains rejected through material and bake");
                    const auto values = std::bit_cast<std::array<float, 36>>(actualBake[index]);
                    for (unsigned c = 0; c < 36; ++c)
                    {
                        Near(values[c], c == 7 ? -1 : 0, "Zero background radiance");
                    }
                    continue;
                }
                ++frameCovered;
                frameFar += reference.triangle == 1;
                Check(reference.covered, "Culling cannot add geometric coverage");
                const auto expectedFrame = std::bit_cast<std::array<float, 20>>(reference.point);
                const auto actualFrame = std::bit_cast<std::array<float, 20>>(actual[index]);
                for (unsigned c = 0; c < 20; ++c)
                {
                    if (c == 3)
                    {
                        // Raster clipping/subpixel interpolation perturbs finite differences;
                        // log2 amplifies that transport error. Material arithmetic stays 1e-4.
                        const double difference = std::abs(double(actualFrame[c]) - expectedFrame[c]);
                        maxLodError = std::max(maxLodError, difference);
                        Check(std::isfinite(actualFrame[c]) && difference <= 5e-4, "Raster LOD transport bound");
                        ++gpuComponents;
                        continue;
                    }
                    Near(actualFrame[c], expectedFrame[c],
                         "Perspective world frame/LOD frame=" + std::to_string(frame) +
                             " pixel=" + std::to_string(index) + " component=" + std::to_string(c));
                }
                for (unsigned c = 0; c < 4; ++c)
                {
                    Near(mapped[4].At(x, y, c), reference.derivatives[c], "Independent fine UV derivative");
                }
                fractionalLods += actual[index].uvLod[3] > .01f && actual[index].uvLod[3] < .99f;
                const auto expectedMaterial = ExpectedPoint(actual[index], view, 1.3f, .5f, tier != 0);
                const auto expectedValues = std::bit_cast<std::array<float, 44>>(expectedMaterial);
                const auto actualValues = std::bit_cast<std::array<float, 44>>(actualMaterial[index]);
                for (unsigned c = 0; c < 44; ++c)
                {
                    const double tolerance = c < 3 ? 1e-3 : 1e-4;
                    Near(actualValues[c], expectedValues[c], "Raster graph material", tolerance);
                }
                // Scalar integration is expensive; independently compare a spread of covered pixels.
                if (index % 79 == 0)
                {
                    const auto expectedBake =
                        std::bit_cast<std::array<float, 36>>(ExpectedBake(expectedMaterial, environmentColors, table));
                    const auto values = std::bit_cast<std::array<float, 36>>(actualBake[index]);
                    for (unsigned c = 0; c < 36; ++c)
                    {
                        Near(values[c], expectedBake[c], "Raster exact pixel IBL");
                    }
                }
            }
        }
        Check(frameBackground > 0, "Background was exercised");
        if (!culling)
        {
            Check(frameCovered > 100, "Visible raster was exercised");
            Check(frame == 2 ? frameFar > 0 : frameFar == 0,
                  "Near clipping exposes far geometry; depth hides it otherwise");
        }
        else
        {
            culledCounts[frame - 4] = frameCovered;
        }
        coveredPixels += frameCovered;
        backgroundPixels += frameBackground;
        farPixels += frameFar;
        std::vector<SurfacePoint> corrupt(actual, actual + count);
        const auto background =
            std::ranges::find_if(corrupt, [](const auto& point) { return point.position[3] == -1; });
        Check(background != corrupt.end(), "Background mutation candidate");
        background->uvLod[0] = 1;
        Check(!raster->ValidateReadback(corrupt, error) && raster->IsValidated(),
              "Background payload corruption is rejected without erasing accepted coverage");
        for (const unsigned workers : {0u, 1u, 4u})
        {
            for (unsigned policy = 0; policy < (mode == "--rg5-reference" ? 3u : 1u); ++policy)
            {
                RunGraphChain(device, pool, textures, meshEvaluator, bindings, collector, evaluators[tier], baker,
                              instances[tier], cube, request, sources, {actual, count}, {actualMaterial, count},
                              {actualBake, count}, workers,
                              policy == 0 ? RGSchedulingMode::DeclarationOrder : RGSchedulingMode::ExplicitVersioned,
                              policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
            }
        }
        if (frame == 6)
        {
            RunGraphFailureCases(device, pool, textures, bindings, collector, evaluators[tier], baker, instances[tier],
                                 cube, request, sources);
        }
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "GPU validation: " + messages);
        for (auto& readback : readbacks)
        {
            device.ReleaseReadback(readback);
        }
    }
    Check((culledCounts[0] == 0 && culledCounts[1] > 100) || (culledCounts[1] == 0 && culledCounts[0] > 100),
          "Back-face culling removes exactly one winding");
    Check(fractionalLods > 0, "Fractional explicit texture LOD exercised");
    for (unsigned fixture = 0; fixture < 4; ++fixture)
    {
        for (bool reverseOrder : {false, true})
        {
            for (unsigned workers : {0u, 1u, 4u})
            {
                for (unsigned policy = 0; policy < (mode == "--rg5-reference" ? 3u : 1u); ++policy)
                {
                    RunSharedDepthChain(device, pool, textures, meshEvaluator, bindings, collectors[0], evaluators, baker,
                                        instances, cube, environmentColors, table, fixture, reverseOrder, workers,
                                        policy == 0 ? RGSchedulingMode::DeclarationOrder : RGSchedulingMode::ExplicitVersioned,
                                        policy == 1 ? RGOrderPolicy::PreserveDeclarationOrder : RGOrderPolicy::DependencyOrder);
                }
            }
        }
    }
    {
        auto geometry = MakeGeometry(false, false);
        geometry.draw.modelMeshView.indexData = geometry.indices.data();
        std::array<float, 6> lods{};
        const SurfaceView view{{0, 0, 2, 0}, 1, 99, 99};
        std::shared_ptr<const MeshSurfaceInput> input;
        std::shared_ptr<const MeshSurfaceBatch> mesh;
        std::shared_ptr<const RasterSurfaceBatch> batch;
        Drain drain{device};
        Check(MeshSurfaceInput::Seal(geometry.draw, view, lods, input, error), "Abort seal");
        Check(device.BeginFrame(error), "Abort prepare begin");
        Check(meshEvaluator.Record(device, input, mesh, error), "Abort mesh");
        RasterSurfaceRequest request;
        request.width = request.height = 8;
        request.viewProjection = math::matrix4x4::identity();
        request.texture = {{}, {}, 4, 2, 2, 0};
        Check(collectors[0].Prepare(device, request, {&mesh, 1}, batch, error), "Abort raster");
        const auto previous = batch;
        device.flushOnUpload = true;
        Check(!collectors[0].Prepare(device, request, {&mesh, 1}, batch, error) && batch == previous,
              "Native prefix during preparation preserves prior batch");
        EnhancedRenderGraph staleGraph(device);
        Check(!batch->Declare(staleGraph, error), "Native prefix invalidates prepared descriptors");
        device.AbortFrame();
        Check(device.BeginFrame(error), "Abort reuse begin");
        EnhancedRenderGraph nextGraph(device);
        Check(!batch->Declare(nextGraph, error), "Aborted preparation cannot cross recordings");
        device.AbortFrame();
    }
    RunCurrentMeshFailures(device, pool, meshEvaluator);
    std::string tailMessages;
    const auto tailErrors = device.DrainDebugMessages(tailMessages);
    Check(tailErrors == 0, "GPU validation after native prefix/abort: " + tailMessages);
    bindings.Clear();
    textures.Shutdown();
    pipelines.Shutdown();
#ifndef LX_PROBE_VULKAN
    roots.Shutdown();
#endif
    pool.Shutdown();
    device.Shutdown();
    std::cout << "LX_MATERIAL_RASTER_SURFACE_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " frames=7 compiled=24 covered=" << coveredPixels << " background=" << backgroundPixels
              << " far=" << farPixels << " fractionalLods=" << fractionalLods << " maxNormalizedError=" << maxError
              << " maxSrgbError=" << maxColorError << " maxLodError=" << maxLodError << " graphFrames=" << graphFrames
              << " graphLists=" << graphLists << " graphFailures=" << graphFailures
              << " sharedDepthFrames=" << sharedDepthFrames << " sharedDepthPixels=" << sharedDepthPixels
              << " coplanarPixels=" << coplanarPixels << " skinnedDepthFrames=" << skinnedDepthFrames
              << " meshFailures=" << meshFailures << " sceneInputFrames=" << sceneInputFrames
              << " sceneInputFailures=" << sceneInputFailures << " sceneCompositionFrames=" << sceneCompositionFrames
              << " sceneCompositionPixels=" << sceneCompositionPixels
              << " sceneCompositionFailures=" << sceneCompositionFailures << " sceneLookupBaked=" << sceneLookupBaked
              << " sceneLookupReused=" << sceneLookupReused
              << " sceneLookupMutationFrames=" << sceneLookupMutationFrames
              << " sceneLookupFullFrames=" << sceneLookupFullFrames
              << " sceneLookupFullPixels=" << sceneLookupFullPixels << " sceneLookupFullColdMs=" << sceneLookupFullMs[0]
              << " sceneLookupFullWarmMs=" << sceneLookupFullMs[1] << " sceneTextureFrames=" << sceneTextureFrames
              << " sceneTexturePixels=" << sceneTexturePixels
              << " sceneTextureFractionalLods=" << sceneTextureFractionalLods
              << " sceneTextureClampedLods=" << sceneTextureClampedLods
              << " maxTextureFilterError=" << maxTextureFilterError
              << " sceneGenerationFrames=" << sceneGenerationFrames
              << " sceneGenerationFallbacks=" << sceneGenerationFallbacks
               << " sceneGenerationRejectedRequests=" << sceneGenerationRejectedRequests
              << " sceneGenerationAborts=" << sceneGenerationAborts
              << " sceneGenerationPendingSubmissions=" << sceneGenerationPendingSubmissions
              << " sceneGenerationStale=" << sceneGenerationStale
              << " sceneGenerationCompiles=" << sceneGenerationCompiles
              << " sceneGenerationWorkers=" << sceneGenerationWorkers
              << " sceneGenerationPsoWorkers=" << sceneGenerationPsoWorkers
              << " sceneLookupApproximateFrames=" << sceneLookupApproximateFrames
              << " sceneLookupApproximated=" << sceneLookupApproximated
              << " sceneLookupApproximatePixels=" << sceneLookupApproximatePixels << '\n';
}
} // namespace

int main(int argc, char** argv)
{
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(ReportAssertion);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* exception) -> LONG {
        std::cerr << "Native exception code=" << std::hex << exception->ExceptionRecord->ExceptionCode
                  << " address=" << exception->ExceptionRecord->ExceptionAddress << std::dec << '\n';
        char message[] = "Unhandled native exception";
        ReportAssertion(0, message, nullptr);
        return EXCEPTION_EXECUTE_HANDLER;
    });
#endif
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    try
    {
        if (argc == 3 && std::string_view(argv[2]) == "--export-cook-fixtures")
        {
            const auto root = std::filesystem::absolute(argv[1]);
            auto* paths = InternalPath::GetInstance();
            paths->BaseProjectPath = root / "Dynamic_CPP";
            paths->CacheRoot = root / "Build/Obj/MaterialProductProbe/SceneCache";
            paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
            Product(root, false);
            Product(root, true);
            std::cout << "LX_SCENE_COOK_FIXTURES_OK graphs=2\n";
            return 0;
        }
        Check(argc == 2 || (argc == 4 && std::string_view(argv[2]) == "--cooked-scene") ||
                  (argc == 3 &&
                   (std::string_view(argv[2]) == "--rg5-reference" || std::string_view(argv[2]) == "--rg5-decal" || std::string_view(argv[2]) == "--rg5-mixed" || std::string_view(argv[2]) == "--forward-transport" || std::string_view(argv[2]) == "--forward-blend" || std::string_view(argv[2]) == "--scene-only" || std::string_view(argv[2]) == "--scene-full-only" ||
                    std::string_view(argv[2]) == "--issue-resource-lifetime" ||
                    std::string_view(argv[2]) == "--issue-admission" ||
                    std::string_view(argv[2]) == "--subsurface-only" ||
                    std::string_view(argv[2]) == "--refraction-only" || std::string_view(argv[2]) == "--volume-only" ||
                    std::string_view(argv[2]) == "--shadow-decal-only" || std::string_view(argv[2]) == "--decal-only")),
              "Expected repository root");
        Run(std::filesystem::absolute(argv[1]), argc >= 3 ? std::string_view(argv[2]) : std::string_view{},
            argc == 4 ? std::filesystem::absolute(argv[3]) : std::filesystem::path{});
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_RASTER_SURFACE_FAIL " << error.what() << '\n';
        return 1;
    }
}
