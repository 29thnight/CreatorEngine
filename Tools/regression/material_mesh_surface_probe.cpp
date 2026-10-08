#include "material_owner_checks.h"
#include "MaterialGraphMeshSurface.h"
#include "support/MaterialGraphScenePacket.h"
#include "PathFinder.h"
#include "Texture.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#include "RHI/DX12/DX12GpuProfiler.h"
#include "RHI/DX12/DX12CommandListPool.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "material_ibl_reference.h"

#include <bit>
#include <iostream>
#include <limits>

namespace
{
using namespace material_graph;
using namespace LX;
using namespace MaterialProbe::IblReference;
std::size_t checks{}, gpuComponents{};
double maxError{}, maxColorError{};
constexpr unsigned kPoints = 37;
constexpr unsigned kSamples = 41;
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
    const auto file = root / "Build/Obj/MaterialProductProbe" / (layered ? "mesh-layered.slang" : "mesh-core.slang");
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
    const auto wrong = root / "Build/Obj/MaterialProductProbe/mesh-wrong.slang";
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
    const auto& pixel = input.uvLod[3] >= .5f
                            ? kMip[unsigned(input.uvLod[0] * 2)]
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
    // Core and Layered graphs both use compensated Principled GGX (IblBakePoint model 1).
    result.viewTier = Pack4(Unit(Rgb4(view.eye) - Rgb4(input.position)), 1);
    return result;
}

struct Geometry
{
    std::vector<std::byte> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<math::matrix4x4> bones;
    std::vector<float> lods;
    EnhancedDrawItem draw;
};

template<class T>
void Attribute(Geometry& geometry, unsigned vertex, assets::VertexAttribute attribute, const T& value)
{
    const auto& view = geometry.draw.modelMeshView;
    std::memcpy(geometry.vertices.data() + vertex * view.vertexStride +
                    assets::OffsetOf(view.vertexAttributeMask, attribute),
                &value, sizeof(value));
}

Geometry GeometryFixture(unsigned mask, unsigned pose)
{
    Geometry result;
    auto& view = result.draw.modelMeshView;
    Check(Uuid::TryParse("11111111-1111-8111-8111-111111111111", view.handle.modelId), "Model identity");
    Check(Uuid::TryParse("22222222-2222-8222-8222-222222222222", view.handle.meshId), "Mesh identity");
    view.handle.generation = 1;
    view.vertexAttributeMask = mask;
    view.vertexStride = assets::StrideOf(mask);
    view.vertexLayoutHash = assets::VertexLayoutHash(mask);
    result.vertices.resize(kPoints * view.vertexStride);
    view.vertexBytes = result.vertices.size();
    view.vertexData = result.vertices.data();
    result.lods.resize(kPoints);
    const auto reference = Points();
    for (unsigned i = 0; i < kPoints; ++i)
    {
        const double angle = (i - 1) * 2 * 3.141592653589793 / (kPoints - 1);
        const std::array<float, 3> position =
            i == 0 ? std::array<float, 3>{0, 0, .2f}
                   : std::array<float, 3>{float(.5 * std::cos(angle)), float(.5 * std::sin(angle)), .2f};
        Attribute(result, i, assets::VertexAttribute::Position, position);
        Attribute(result, i, assets::VertexAttribute::Normal, std::array<float, 3>{0, 0, 1});
        Attribute(result, i, assets::VertexAttribute::Uv0,
                  std::array<float, 2>{reference[i].uvLod[0], reference[i].uvLod[1]});
        Attribute(result, i, assets::VertexAttribute::Tangent,
                  i == 0 ? std::array<float, 4>{0, 0, 0, 1} : std::array<float, 4>{1, .12f, 0, i % 2 ? -1.f : 1.f});
        if (assets::Has(mask, assets::VertexAttribute::Uv1))
        {
            Attribute(result, i, assets::VertexAttribute::Uv1, std::array<float, 2>{.9f, .3f});
        }
        if (assets::Has(mask, assets::VertexAttribute::Color))
        {
            Attribute(result, i, assets::VertexAttribute::Color, std::array<float, 4>{.2f, .4f, .6f, .8f});
        }
        if (assets::Has(mask, assets::VertexAttribute::BoneIndices))
        {
            Attribute(result, i, assets::VertexAttribute::BoneIndices,
                      i == 0       ? std::array<std::uint8_t, 4>{255, 255, 255, 255}
                      : i % 3 == 1 ? std::array<std::uint8_t, 4>{0, 255, 255, 255}
                                   : std::array<std::uint8_t, 4>{0, 1, 255, 255});
            Attribute(result, i, assets::VertexAttribute::BoneWeights,
                      i == 0       ? std::array<float, 4>{}
                      : i % 3 == 1 ? std::array<float, 4>{1, 0, 0, 0}
                                   : std::array<float, 4>{.25f, .75f, 0, 0});
        }
        result.lods[i] = reference[i].uvLod[3];
        if (i > 0)
        {
            result.indices.insert(result.indices.end(), {0, i, i == kPoints - 1 ? 1 : i + 1});
        }
    }
    view.indexData = result.indices.data();
    view.indexCount = static_cast<unsigned>(result.indices.size());
    result.draw.geometryKey = HashModelMeshHandle(view.handle);
    auto& world = result.draw.worldMatrix;
    world = math::matrix4x4::identity();
    world.m[0][0] = pose == 2 ? -.8f : .8f;
    world.m[1][1] = .7f;
    world.m[2][2] = 1.1f;
    world.m[0][1] = .13f;
    world.m[1][2] = .15f;
    world.m[3][0] = pose == 1 ? .1f : 0;
    if (assets::Has(mask, assets::VertexAttribute::BoneIndices) && pose != 0)
    {
        result.bones = {math::matrix4x4::identity(), math::matrix4x4::identity()};
        result.bones[0].m[3][1] = .04f;
        result.bones[1].m[0][0] = .96f;
        result.bones[1].m[0][1] = .28f;
        result.bones[1].m[1][0] = -.28f;
        result.bones[1].m[1][1] = .96f;
        result.bones[1].m[3][0] = -.06f;
        result.draw.boneCount = static_cast<unsigned>(result.bones.size());
        result.draw.bonePalette = result.bones.data();
    }
    return result;
}

// Independent double scalar reference, not a call into the product transform.
using Matrix = std::array<std::array<double, 4>, 4>;
Matrix MatrixOf(const math::matrix4x4& source)
{
    Matrix result;
    for (unsigned r = 0; r < 4; ++r)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            result[r][c] = source.m[r][c];
        }
    }
    return result;
}
Matrix Multiply(const Matrix& a, const Matrix& b)
{
    Matrix result{};
    for (unsigned r = 0; r < 4; ++r)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            for (unsigned k = 0; k < 4; ++k)
            {
                result[r][c] += a[r][k] * b[k][c];
            }
        }
    }
    return result;
}
Vector Transform(Vector source, const Matrix& matrix, double w)
{
    Vector result;
    for (unsigned c = 0; c < 3; ++c)
    {
        result[c] = source[0] * matrix[0][c] + source[1] * matrix[1][c] + source[2] * matrix[2][c] + w * matrix[3][c];
    }
    return result;
}
SurfacePoint WorldReference(const Geometry& geometry, unsigned index)
{
    const auto& view = geometry.draw.modelMeshView;
    const auto read = [&](assets::VertexAttribute attribute, void* target, std::size_t bytes) {
        std::memcpy(target,
                    geometry.vertices.data() + index * view.vertexStride +
                        assets::OffsetOf(view.vertexAttributeMask, attribute),
                    bytes);
    };
    std::array<float, 3> position, normal;
    std::array<float, 4> tangent;
    std::array<float, 2> uv;
    read(assets::VertexAttribute::Position, position.data(), sizeof(position));
    read(assets::VertexAttribute::Normal, normal.data(), sizeof(normal));
    read(assets::VertexAttribute::Tangent, tangent.data(), sizeof(tangent));
    read(assets::VertexAttribute::Uv0, uv.data(), sizeof(uv));
    auto world = MatrixOf(geometry.draw.worldMatrix);
    if (!geometry.bones.empty())
    {
        std::array<float, 4> weights;
        std::array<std::uint8_t, 4> indices;
        read(assets::VertexAttribute::BoneWeights, weights.data(), sizeof(weights));
        read(assets::VertexAttribute::BoneIndices, indices.data(), sizeof(indices));
        if (weights[0] > 0)
        {
            Matrix skin{};
            for (unsigned b = 0; b < 4; ++b)
            {
                if (weights[b] == 0)
                {
                    continue;
                }
                for (unsigned r = 0; r < 4; ++r)
                {
                    for (unsigned c = 0; c < 4; ++c)
                    {
                        skin[r][c] += weights[b] * geometry.bones[indices[b]].m[r][c];
                    }
                }
            }
            world = Multiply(skin, world);
        }
    }
    const Vector n{normal[0], normal[1], normal[2]}, t{tangent[0], tangent[1], tangent[2]};
    const Vector b = Cross(n, t) * tangent[3];
    double scale = 0;
    for (unsigned r = 0; r < 3; ++r)
    {
        for (unsigned c = 0; c < 3; ++c)
        {
            scale = std::max(scale, std::abs(world[r][c]));
        }
    }
    auto linear = world;
    for (unsigned r = 0; r < 4; ++r)
    {
        for (unsigned c = 0; c < 4; ++c)
        {
            linear[r][c] = r < 3 && c < 3 ? world[r][c] / scale : 0;
        }
    }
    const Vector row0{linear[0][0], linear[0][1], linear[0][2]};
    const Vector row1{linear[1][0], linear[1][1], linear[1][2]};
    const Vector row2{linear[2][0], linear[2][1], linear[2][2]};
    const auto c0 = Cross(row1, row2), c1 = Cross(row2, row0), c2 = Cross(row0, row1);
    const auto normalOut = Unit((c0 * n[0] + c1 * n[1] + c2 * n[2]) * (Dot(row0, c0) < 0 ? -1 : 1));
    SurfacePoint result;
    result.uvLod = {uv[0], uv[1], 0, geometry.lods[index]};
    result.position = Pack4(Transform({position[0], position[1], position[2]}, world, 1), 0);
    result.normal = Pack4(normalOut, 0);
    result.tangent = Pack4(Transform(t, linear, 0), 0);
    result.bitangent = Pack4(Transform(b, linear, 0), 0);
    return result;
}

Geometry LargeGeometry(unsigned mask)
{
    auto result = GeometryFixture(mask, 2);
    const auto original = result.vertices;
    const auto originalLods = result.lods;
    constexpr unsigned count = 8200;
    const auto stride = result.draw.modelMeshView.vertexStride;
    result.vertices.resize(count * stride);
    result.lods.resize(count);
    for (unsigned i = 0; i < count; ++i)
    {
        std::memcpy(result.vertices.data() + i * stride, original.data() + (i % kPoints) * stride, stride);
        result.lods[i] = originalLods[i % kPoints];
    }
    result.indices.clear();
    // Shared hub, duplicate-valued seam vertices, one unused vertex, and
    // degenerate triangles. Partition must preserve identities and winding.
    for (unsigned i = 1; i < count - 2; ++i)
    {
        result.indices.insert(result.indices.end(), {0, i, i + 1});
    }
    result.indices.insert(result.indices.end(), {1, 1, 2, 3, 4, 3});
    auto& view = result.draw.modelMeshView;
    view.vertexData = result.vertices.data();
    view.vertexBytes = result.vertices.size();
    view.indexData = result.indices.data();
    view.indexCount = static_cast<unsigned>(result.indices.size());
    result.draw.bonePalette = result.bones.empty() ? nullptr : result.bones.data();
    return result;
}

void VerifyPartition(const Geometry& geometry, const MeshSurfacePlan& plan, unsigned limit)
{
    unsigned triangle = 0;
    std::uint64_t points = 0;
    for (const auto& chunk : plan.Chunks())
    {
        const auto& input = *chunk.input;
        const auto& local = input.Geometry();
        Check(local.IsComplete() && input.Count() <= limit && chunk.sourceVertices.size() == input.Count(),
              "Complete bounded partition");
        Check(chunk.firstTriangle == triangle, "Contiguous original triangle order");
        Check(local.handle == geometry.draw.modelMeshView.handle, "Partition generation identity");
        for (unsigned i = 0; i < input.Count(); ++i)
        {
            const auto source = chunk.sourceVertices[i];
            Check(source < geometry.lods.size() && input.Lods()[i] == geometry.lods[source], "Exact LOD remap");
            Check(std::memcmp(static_cast<const std::byte*>(local.vertexData) + i * local.vertexStride,
                              geometry.vertices.data() + source * local.vertexStride, local.vertexStride) == 0,
                  "Every product attribute preserved, including UV/color/skin seams");
        }
        for (unsigned i = 0; i < local.indexCount; ++i)
        {
            Check(local.indexData[i] < input.Count() &&
                      chunk.sourceVertices[local.indexData[i]] == geometry.indices[std::size_t(triangle) * 3 + i],
                  "Original triangle and winding reconstruction");
        }
        triangle += local.indexCount / 3;
        points += input.Count();
    }
    Check(triangle == geometry.indices.size() / 3 && points == plan.Cost().points, "No lost or repeated triangles");
}

std::shared_ptr<const MeshSurfacePlan> PartitionTests(const SurfaceView& view)
{
    std::string error;
    std::shared_ptr<const MeshSurfacePlan> large;
    for (auto mask : assets::kModelVertexMasks)
    {
        auto geometry = LargeGeometry(mask);
        Check(MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, {}, large, error),
              "Large partition: " + error);
        VerifyPartition(geometry, *large, IblBaker::MaxPoints);
        Check(large->Chunks().size() == 3 && large->Cost().referencedVertices == 8199 &&
                  large->Cost().sourceVertices == 8200 && large->Cost().duplicatedPoints > 0,
              "Large fan splits at boundary and excludes only the unused vertex");
        const auto accepted = large;
        std::shared_ptr<const MeshSurfacePlan> same;
        Check(MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, {}, same, error) && large->Matches(*same),
              "Deterministic partition identity");
        std::vector<float> sceneLods(geometry.lods.size(), 0.f);
        std::shared_ptr<const MeshSurfacePlan> sceneFirst, sceneNext;
        Check(MeshSurfacePlan::BuildForScene(geometry.draw, view, sceneLods, {}, sceneFirst, error),
              "Scene partition first seal: " + error);
        auto nextView = view;
        ++nextView.viewRevision;
        nextView.eye[0] += 2.f;
        auto movedDraw = geometry.draw;
        movedDraw.worldMatrix.m[3][0] += 3.f;
        Check(MeshSurfacePlan::BuildForScene(movedDraw, nextView, sceneLods, {}, sceneNext, error),
              "Scene partition pose rebind: " + error);
        Check(sceneNext->Cost().points == sceneFirst->Cost().points &&
                  sceneNext->Chunks().size() == sceneFirst->Chunks().size() &&
                  sceneNext->Source()->Geometry().vertexData == sceneFirst->Source()->Geometry().vertexData &&
                  sceneNext->Source()->World().m[3][0] == movedDraw.worldMatrix.m[3][0] &&
                  sceneNext->Source()->View().eye == nextView.eye &&
                  !sceneNext->Matches(*sceneFirst),
              "Scene reuse retains immutable bytes while sealing current world and camera");
        for (size_t chunk = 0; chunk < sceneFirst->Chunks().size(); ++chunk)
        {
            Check(sceneNext->Chunks()[chunk].sourceVertices.data() == sceneFirst->Chunks()[chunk].sourceVertices.data(),
                  "Scene cache hits share immutable remap storage");
        }
        std::shared_ptr<const MeshSurfacePlan> rasterPlan;
        Check(MeshSurfacePlan::BuildForScene(movedDraw, nextView, {}, rasterPlan, error) &&
                  rasterPlan->Matches(*sceneNext),
              "Raster zero LOD transport reuses the same validated mesh and current pose");
        sceneLods.pop_back();
        Check(!MeshSurfacePlan::BuildForScene(movedDraw, nextView, sceneLods, {}, rasterPlan, error),
              "Cached scene plan still rejects an incomplete explicit LOD transport");
        MeshSurfacePlanBudget rasterBudget;
        rasterBudget.maxChunkPoints = rasterBudget.maxSourceVertices;
        const auto originalLods = geometry.lods;
        std::fill(geometry.lods.begin(), geometry.lods.end(), 0.f);
        Check(MeshSurfacePlan::BuildForScene(geometry.draw, view, rasterBudget, rasterPlan, error) &&
                  rasterPlan->Chunks().size() == 1 && rasterPlan->Cost().points == 8199,
              "Raster partition is independent of the IBL compute point limit");
        VerifyPartition(geometry, *rasterPlan, rasterBudget.maxChunkPoints);
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, rasterBudget, same, error),
              "IBL batch retains its original compute point limit");
        geometry.lods = originalLods;
        MeshSurfacePlanBudget budget;
        budget.maxCpuPayloadBytes = large->Cost().cpuPayloadBytes;
        budget.maxGpuPayloadBytes = large->Cost().gpuPayloadBytes;
        Check(MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, same, error),
              "Exact payload budget accepts");
        --budget.maxGpuPayloadBytes;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, large, error) && large == accepted,
              "GPU payload overflow preserves entire accepted plan");
        budget = {};
        budget.maxCpuPayloadBytes = accepted->Cost().cpuPayloadBytes - 1;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, large, error) && large == accepted,
              "CPU payload overflow preserves plan");
        budget = {};
        budget.maxChunks = 2;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, large, error) && large == accepted,
              "Chunk overflow preserves plan");
        budget = {};
        budget.maxSourceVertices = 8199;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, large, error),
              "Source budget rejects");
        const auto last = geometry.indices.back();
        geometry.indices.back() = 8200;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, {}, large, error) && large == accepted,
              "Late invalid triangle preserves all earlier accepted chunks");
        geometry.indices.back() = last;
        auto changed = view;
        ++changed.viewRevision;
        Check(MeshSurfacePlan::Build(geometry.draw, changed, geometry.lods, {}, same, error) && !large->Matches(*same),
              "View revision invalidates partition reuse");
        geometry = GeometryFixture(mask, 0);
        for (auto limit : {3u, 7u})
        {
            budget = {};
            budget.maxChunkPoints = limit;
            Check(MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, same, error), "Small chunk limit");
            VerifyPartition(geometry, *same, limit);
            Check(!large->Matches(*same), "Partition limit/source changes identity");
        }
        budget.maxChunkPoints = 2;
        Check(!MeshSurfacePlan::Build(geometry.draw, view, geometry.lods, budget, same, error),
              "Triangle minimum limit");
    }
    return large;
}

std::vector<MeshSurfaceSample> SampleRequests()
{
    std::string error;
    float lod = 99;
    Check(ResolveSurfaceLod({{.5f, 0}, {0, .5f}, 4, 2, 2}, lod, error) && lod == 1, "Two-texel footprint LOD");
    Check(ResolveSurfaceLod({{.25f, 0}, {0, .5f}, 4, 2, 2}, lod, error) && lod == 0, "One-texel footprint LOD");
    Check(ResolveSurfaceLod({{0, 0}, {0, 0}, 4, 2, 2}, lod, error) && lod == 0, "Magnification clamps to LOD0");
    Check(ResolveSurfaceLod({{100, 0}, {0, 100}, 4, 2, 2}, lod, error) && lod == 1, "Minification mip limit");
    Check(ResolveSurfaceLod({{.25f, 0}, {0, .5f}, 4, 2, 2, .5f}, lod, error) && lod == .5f, "LOD bias");
    const float accepted = lod;
    Check(!ResolveSurfaceLod({{.25f, 0}, {0, .5f}, 4, 2, 4}, lod, error) && lod == accepted,
          "Impossible mip extent preserves LOD");
    Check(!ResolveSurfaceLod({{std::numeric_limits<float>::quiet_NaN(), 0}, {}, 4, 2, 2}, lod, error),
          "Nonfinite raster derivative rejects");
    std::vector<MeshSurfaceSample> result;
    for (unsigned i = 0; i < kSamples; ++i)
    {
        const float b = .05f + float(i % 4) * .15f;
        const float c = .05f + float((i / 4) % 3) * .1f;
        const float footprint = i % 9 == 0   ? float(std::exp2(.75) / 4)
                                : i % 9 == 1 ? float(std::exp2(.25) / 4)
                                : i % 7 == 0 ? .5f
                                             : .1f;
        Check(ResolveSurfaceLod({{footprint, 0}, {0, .1f}, 4, 2, 2}, lod, error), "Sample footprint");
        result.push_back({i % 3, b, c, lod});
    }
    result.back() = {0, .6f, .4f, 0};
    return result;
}

SurfacePoint SampleReference(const Geometry& geometry, const MeshSurfaceSample& sample)
{
    std::array<double, 20> result{};
    const std::array<double, 3> weights{1.0 - sample.b - sample.c, sample.b, sample.c};
    for (unsigned corner = 0; corner < 3; ++corner)
    {
        const auto index = geometry.indices[std::size_t(sample.triangle) * 3 + corner];
        const auto point = std::bit_cast<std::array<float, 20>>(WorldReference(geometry, index));
        for (unsigned c = 0; c < 20; ++c)
        {
            result[c] += weights[corner] * point[c];
        }
    }
    std::array<float, 20> point;
    std::ranges::transform(result, point.begin(), [](double value) { return float(value); });
    point[3] = sample.lod;
    return std::bit_cast<SurfacePoint>(point);
}

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

class RecordingChangeDevice final : public DX12DeviceResources
{
  public:
    bool flushOnUpload{};
    RHIBufferSlice AllocateUpload(const RHIUploadRequest& request) override
    {
        if (flushOnUpload)
        {
            flushOnUpload = false;
            std::string error;
            Check(FlushCommandList(error), "Injected native prefix submission: " + error);
        }
        return DX12DeviceResources::AllocateUpload(request);
    }
};

void VerifyPartitionGpu(DX12DeviceResources& device, MeshSurfaceEvaluator& evaluator, const MeshSurfacePlan& plan,
                        bool raster = false)
{
    auto original = LargeGeometry(assets::kModelVertexMasks.back());
    if (raster) std::fill(original.lods.begin(), original.lods.end(), 0.f);
    std::vector<SurfacePoint> expected;
    for (unsigned i = 0; i < original.lods.size(); ++i)
    {
        expected.push_back(WorldReference(original, i));
    }
    std::string error;
    for (const auto& chunk : plan.Chunks())
    {
        RHIReadback readback;
        Check(device.CreateBufferReadback(chunk.input->Count() * sizeof(SurfacePoint), readback, error),
              "Partition readback");
        Check(device.BeginFrame(error), "Partition begin");
        std::shared_ptr<const MeshSurfaceBatch> result;
        Check(evaluator.Record(device, chunk.input, result, error), "Partition GPU transform " + error);
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferTransition before{result->Buffer(), RHIResourceState::ShaderResource,
                                         RHIResourceState::CopySource};
        encoder.ResourceBarriers({{}, {&before, 1}});
        encoder.CopyBufferToReadback(readback, result->Buffer());
        Check(device.EndFrame(error), "Partition submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Partition native submit");
        device.WaitForGpu();
        RHIReadbackImage mapped;
        Check(device.MapReadback(readback, mapped, error), "Partition map");
        const auto actual = mapped.Elements<SurfacePoint>();
        Check(result->ValidateReadback({actual, result->Count()}, error), "Partition completed acceptance " + error);
        for (unsigned i = 0; i < result->Count(); ++i)
        {
            const auto point = std::bit_cast<std::array<float, 20>>(actual[i]);
            const auto reference = std::bit_cast<std::array<float, 20>>(expected[chunk.sourceVertices[i]]);
            for (unsigned c = 0; c < 20; ++c)
            {
                Near(point[c], reference[c], "Partition seam/source scalar reference");
            }
        }
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "Partition GPU validation: " + messages);
        device.ReleaseReadback(readback);
    }
}

void VerifyCacheAdmissionGpu(DX12DeviceResources& device, MeshSurfaceEvaluator& evaluator, SurfaceView view)
{
    auto geometry = GeometryFixture(assets::kCoreVertexAttributes, 0);
    const auto baseline = evaluator.CacheStats();
    std::shared_ptr<const MeshSurfaceBatch> retained;
    std::string error;
    for (unsigned entry = 0; entry < 260; ++entry)
    {
        geometry.draw.modelMeshView.handle.generation = 10000 + entry;
        std::shared_ptr<const MeshSurfaceInput> input;
        Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, input, error), "Cache admission seal");
        Check(device.BeginFrame(error), "Cache admission begin");
        EnhancedRenderGraph graph(device);
        std::shared_ptr<const MeshSurfaceBatch> batch;
        Check(evaluator.Prepare(device, input, batch, error, true), "Cache admission prepare " + error);
        Check(batch->Indices().IsValid() && !batch->Indices().IsWritable(), "Cache admission stays resident");
        Check(batch->Declare(graph, error), "Cache admission declaration");
        const auto output = batch->GraphOutput(graph);
        graph.AddPass("Test.CacheAdmission.Read", {{output, RHIResourceState::ShaderResource}}, [](const auto&) {}, true);
        Check(graph.Compile(error) && graph.Execute(error), "Cache admission graph " + error);
        Check(device.EndFrame(error), "Cache admission submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Cache admission drain");
        device.WaitForGpu();
        batch->MarkSubmitted({device.GetLastSignaledFenceValue()});
        evaluator.NotifyCompleted(device.GetCompletedFenceValue());
        if (entry == 0)
        {
            retained = batch;
        }
    }
    Check(evaluator.CacheStats().uploads == baseline.uploads + 260, "Completed outputs cannot pin static cache admission");
    Check(evaluator.CacheStats().entries <= 256, "Static cache remains bounded");
    Check(retained->Indices().IsValid() && retained->Buffer().IsValid(), "Externally retained geometry survives eviction");
    RHIReadback readback;
    Check(device.CreateBufferReadback(retained->Count() * sizeof(SurfacePoint), readback, error), "Retained cache readback");
    Check(device.BeginFrame(error), "Retained cache begin");
    EnhancedRenderGraph graph(device);
    const auto output = graph.ImportBuffer(retained->Buffer(), RHIResourceState::ShaderResource, "Test.RetainedCache");
    graph.AddPass("Test.RetainedCache.Read", {{output, RHIResourceState::CopySource}}, [&](const auto& context)
    {
        context.encoder->CopyBufferToReadback(readback, retained->Buffer());
    }, true);
    Check(graph.Compile(error) && graph.Execute(error), "Retained cache graph " + error);
    Check(device.EndFrame(error), "Retained cache submit");
    Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Retained cache drain");
    device.WaitForGpu();
    RHIReadbackImage mapped;
    Check(device.MapReadback(readback, mapped, error), "Retained cache map");
    const auto actual = std::bit_cast<std::array<float, 20>>(mapped.Elements<SurfacePoint>()[0]);
    const auto expected = std::bit_cast<std::array<float, 20>>(WorldReference(geometry, 0));
    for (unsigned component = 0; component < actual.size(); ++component)
    {
        Near(actual[component], expected[component], "Retained cache remains readable after eviction");
    }
    device.ReleaseReadback(readback);
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "Cache admission GPU validation: " + messages);
    std::cout << "ISSUE_CACHE_ADMISSION_OK completed=260 retained=1 validation=0\n";
}

void VerifyResidentInputsGpu(DX12DeviceResources& device, MeshSurfaceEvaluator& evaluator, SurfaceView view)
{
    DX12GpuProfiler profiler;
    DX12GpuProfiler::FrameTimings timings;
    timings.ticksPerSecond = 1000;
    timings.slices = {{"A", 0, 10}, {"B", 10, 90}, {"A", 90, 100}};
    std::vector<DX12GpuProfiler::PassTiming> merged;
    profiler.MergeSlices(timings, merged);
    Check(merged.size() == 2 && merged[0].milliseconds == 20 && merged[0].spanMilliseconds == 100 &&
          profiler.GetLastTotalMilliseconds() == 100, "Interleaved GPU pass duration excludes other passes");

    auto geometry = GeometryFixture(assets::kModelVertexMasks.back(), 2);
    std::fill(geometry.lods.begin(), geometry.lods.end(), 0.f);
    const auto baseline = evaluator.CacheStats();
    std::string error;
    thread_pool recordingThreads;
    job_scheduler recordingJobs(recordingThreads);
    recordingJobs.start(4);
    DX12CommandListPool pool(recordingJobs);
    Check(pool.Initialize(device, 4, DX12DeviceResources::kFrameCount, error), "Resident parallel pool");
    const unsigned workerCounts[]{0, 1, 4, 1, 4, 4};
    RHIBufferHandle previousOutput;
    for (unsigned frame = 0; frame < 6; ++frame)
    {
        if (frame == 2) geometry.draw.worldMatrix.m[3][0] += .25f;
        if (frame == 3) geometry.bones[0].m[3][1] += .4f;
        if (frame == 5) ++geometry.draw.modelMeshView.handle.generation;
        ++view.viewRevision;
        view.eye[0] += .1f;
        std::shared_ptr<const MeshSurfacePlan> plan;
        Check(MeshSurfacePlan::BuildForScene(geometry.draw, view, geometry.lods, {}, plan, error), "Resident plan");
        Check(plan->Chunks().size() == 1, "Resident fixture has one partition");
        const auto& chunk = plan->Chunks().front();
        Check(device.BeginFrame(error), "Resident begin");
        auto graphOwner = std::make_shared<EnhancedRenderGraph>(device);
        auto& graph = *graphOwner;
        if (workerCounts[frame])
        {
            pool.BeginFrame(frame);
            Check(graph.PrepareParallel(pool, error), "Resident prepare parallel prefix");
        }
        std::shared_ptr<const MeshSurfaceBatch> batch;
        Check(evaluator.Prepare(device, chunk.input, batch, error, true), "Resident prepare " + error);
        Check(batch->Indices().IsValid() && !batch->Indices().IsWritable(), "Indices are GPU resident");
        if (frame == 1 || frame == 4)
            Check(batch->Buffer() == previousOutput, "Unchanged pose reuses completed world geometry across views");
        else if (frame)
            Check(batch->Buffer() != previousOutput, "World, bones and generation invalidate transformed output");
        Check(batch->View().viewRevision == view.viewRevision && batch->View().eye == view.eye,
              "Reused geometry retains fresh view metadata");
        previousOutput = batch->Buffer();
        RHIReadback vertices, indices;
        Check(device.CreateBufferReadback(batch->Count() * sizeof(SurfacePoint), vertices, error), "Resident readback");
        Check(device.CreateBufferReadback(batch->Indices().size, indices, error), "Resident index readback");
        Check(batch->Declare(graph, error), "Resident graph declaration");
        const auto output = batch->GraphOutput(graph);
        const auto index = graph.ImportBuffer(batch->Indices().buffer, RHIResourceState::IndexBuffer, "Test.Indices");
        graph.AddPass("Test.ResidentReadback", {{output, RHIResourceState::CopySource}, {index, RHIResourceState::CopySource}},
            [&](const auto& context) {
                context.encoder->CopyBufferToReadback(vertices, batch->Buffer());
                context.encoder->CopyBufferToReadback(indices, batch->Indices().buffer);
            }, true);
        graph.AddPass("Test.IndexReady", {{index, RHIResourceState::IndexBuffer}}, [](const auto&) {}, true);
        graph.AddPass("Test.WorldReady", {{output, RHIResourceState::ShaderResource}}, [](const auto&) {}, true);
        Check(graph.Compile(error), "Resident graph compile " + error);
        RHISubmissionTicket ticket;
        if (workerCounts[frame])
        {
            graph.SetParallelRecordCostThreshold(0);
            RHIRecordedBatchDesc description;
            description.frameId = frame + 1;
            description.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
            description.lifetimeToken = graphOwner;
            RHIRecordedBatch recorded;
            const bool recordedOk = graph.RecordParallel(pool, workerCounts[frame], description, recorded, error);
            Check(recordedOk, "Resident parallel recording frame=" + std::to_string(frame) + " " + error);
            Check(GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(recorded), ticket, error),
                  "Resident parallel submit " + error);
        }
        else Check(graph.Execute(error), "Resident graph execution " + error);
        Check(device.EndFrame(error), "Resident submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Resident native submit");
        device.WaitForGpu();
        batch->MarkSubmitted({device.GetLastSignaledFenceValue()});
        evaluator.NotifyCompleted(device.GetCompletedFenceValue());
        RHIReadbackImage mapped;
        Check(device.MapReadback(vertices, mapped, error), "Resident map");
        const auto* actual = mapped.Elements<SurfacePoint>();
        for (unsigned point = 0; point < batch->Count(); ++point)
        {
            const auto got = std::bit_cast<std::array<float, 20>>(actual[point]);
            const auto expected = std::bit_cast<std::array<float, 20>>(WorldReference(geometry, chunk.sourceVertices[point]));
            for (unsigned component = 0; component < 20; ++component)
                Near(got[component], expected[component], "Resident pose/world matches reference");
        }
        Check(device.MapReadback(indices, mapped, error), "Resident indices map");
        Check(std::memcmp(mapped.Elements<std::uint32_t>(), chunk.input->Geometry().indexData, batch->Indices().size) == 0,
              "Resident indices preserve triangle ordering");
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "Resident GPU validation: " + messages);
        device.ReleaseReadback(vertices);
        device.ReleaseReadback(indices);
    }
    Check(evaluator.CacheStats().uploads == baseline.uploads + 2 && evaluator.CacheStats().hits >= baseline.hits + 4,
          "Only geometry generation changes reupload static input");
    Check(evaluator.CacheStats().transforms == baseline.transforms + 4 &&
          evaluator.CacheStats().transformHits == baseline.transformHits + 2,
          "Transform cache dispatches only changed geometry/pose/world");

    auto replacement = GeometryFixture(assets::kCoreVertexAttributes, 0);
    std::shared_ptr<const MeshSurfaceInput> input;
    Check(MeshSurfaceInput::Seal(replacement.draw, view, replacement.lods, input, error), "Replacement identity");
    const auto entries = evaluator.CacheStats().entries;
    Check(device.BeginFrame(error), "Resident abort begin");
    std::shared_ptr<const MeshSurfaceBatch> discarded;
    Check(evaluator.Prepare(device, input, discarded, error, true), "Resident abort prepare");
    device.AbortFrame();
    Check(evaluator.CacheStats().entries == entries, "Aborted uploads cannot become resident cache hits");
    pool.Shutdown();
    std::printf("LX_MATERIAL_RESIDENT_INPUT_OK submitted=6 aborted=1 uploads=2 reuse=4 transforms=4 transformReuse=2 workers=0,1,4\n");
}

void VerifySamplesGpu(DX12DeviceResources& device, DX12TextureCache& textures, RenderBindingCache& bindings,
                      MeshSurfaceEvaluator& meshEvaluator, SurfaceEvaluator& evaluator, IblBaker& baker,
                      const own::shared_owner<const Instance>& instance, const own::shared_owner<const Texture>& cube,
                      const Environment& environmentColors, const SheenTable& table, const SurfaceView& view,
                      unsigned tier, unsigned& frames)
{
    auto geometry = GeometryFixture(assets::kModelVertexMasks.back(), 2);
    for (unsigned i = 0; i < kPoints; ++i)
    {
        Attribute(geometry, i, assets::VertexAttribute::Tangent, std::array<float, 4>{1, .12f, 0, 1});
        Attribute(geometry, i, assets::VertexAttribute::Normal,
                  std::array<float, 3>{.04f * float(i % 3), .03f * float(i % 4), 1});
    }
    const auto requests = SampleRequests();
    std::vector<SurfacePoint> expected;
    for (const auto& request : requests)
    {
        expected.push_back(SampleReference(geometry, request));
    }
    std::string error;
    std::shared_ptr<const MeshSurfaceInput> input;
    Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, input, error), "Sample source seal");
    std::array<RHIReadback, 4> readbacks;
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(device.CreateBufferReadback((i == 0 ? kPoints : kSamples) * sizeof(SurfacePoint), readbacks[i], error),
              "Sample spatial readback");
    }
    Check(device.CreateBufferReadback(kSamples * sizeof(IblBakePoint), readbacks[2], error), "Sample graph readback");
    Check(device.CreateBufferReadback(kSamples * sizeof(IblBakeSample), readbacks[3], error), "Sample bake readback");
    Check(device.BeginFrame(error), "Sample begin");
    textures.BeginFrame(++frames);
    std::shared_ptr<const RenderBindings> materialBindings;
    Check(bindings.Prepare(device, textures, instance, evaluator.Layout(), materialBindings, error), "Sample bindings");
    const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), error), 1, cube};
    std::shared_ptr<const MeshSurfaceBatch> vertices, sampled;
    std::shared_ptr<const SurfaceBatch> surface;
    std::shared_ptr<const IblBakeResult> bake;
    Check(meshEvaluator.Record(device, input, vertices, error), "Sample vertex transform");
    Check(meshEvaluator.RecordSamples(device, vertices, requests, sampled, error), "Triangle sampling " + error);
    Check(!sampled->IsPreparedForGraph() && sampled->IsReadyForEvaluation(),
          "Retained sampler owner does not turn an immediate result into a deferred world transform");
    Check(sampled->Count() == kSamples && sampled->Input()->Count() == kPoints,
          "Exact sample count is independent of its source vertex count");
    const auto accepted = sampled;
    auto bad = requests;
    bad.back().triangle = geometry.draw.modelMeshView.indexCount / 3;
    Check(!meshEvaluator.RecordSamples(device, vertices, bad, sampled, error) && sampled == accepted,
          "Invalid triangle sample preserves batch");
    bad = requests;
    bad.back().b = .9f;
    bad.back().c = .9f;
    Check(!meshEvaluator.RecordSamples(device, vertices, bad, sampled, error) && sampled == accepted,
          "Invalid perspective weights preserve batch");
    bad = requests;
    bad.back().lod = -1;
    Check(!meshEvaluator.RecordSamples(device, vertices, bad, sampled, error) && sampled == accepted,
          "Invalid explicit LOD preserves batch");
    Check(!meshEvaluator.RecordSamples(device, sampled, requests, sampled, error) && sampled == accepted,
          "Sampled points cannot be reinterpreted as a mesh vertex array");
    Check(evaluator.RecordGpu(device, materialBindings, sampled, surface, error),
          "Interpolated frame -> graph " + error);
    Check(baker.RecordGpu(device, environment, surface, bake, error), "Exact sampled material -> IBL " + error);
    auto& encoder = device.GetImmediateEncoder();
    const RHIBufferHandle buffers[]{vertices->Buffer(), sampled->Buffer(), surface->Buffer(), bake->Buffer()};
    for (unsigned i = 0; i < 4; ++i)
    {
        const RHIBufferTransition before{buffers[i], RHIResourceState::ShaderResource, RHIResourceState::CopySource};
        encoder.ResourceBarriers({{}, {&before, 1}});
        encoder.CopyBufferToReadback(readbacks[i], buffers[i]);
        const RHIBufferTransition after{buffers[i], RHIResourceState::CopySource, RHIResourceState::ShaderResource};
        encoder.ResourceBarriers({{}, {&after, 1}});
    }
    Check(device.EndFrame(error), "Sample submit");
    Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Sample native submit");
    device.WaitForGpu();
    std::array<RHIReadbackImage, 4> mapped;
    for (unsigned i = 0; i < 4; ++i)
    {
        Check(device.MapReadback(readbacks[i], mapped[i], error), "Sample map");
    }
    const auto actualVertices = mapped[0].Elements<SurfacePoint>();
    const auto actualPoints = mapped[1].Elements<SurfacePoint>();
    const auto actualMaterial = mapped[2].Elements<IblBakePoint>();
    const auto actualBake = mapped[3].Elements<IblBakeSample>();
    Check(!sampled->ValidateReadback({actualPoints, kSamples}, error), "Sample acceptance requires accepted vertices");
    Check(vertices->ValidateReadback({actualVertices, kPoints}, error), "Vertex source accepted");
    Check(sampled->ValidateReadback({actualPoints, kSamples}, error), "Sample frame accepted " + error);
    Check(surface->ValidateReadback({actualMaterial, kSamples}, error), "Sample material accepted " + error);
    SceneSurfaceEvaluation evaluation;
    Check(BuildSceneSurfaceEvaluation(surface, evaluation, error) && evaluation.gpu->Count() == kSamples &&
              material_graph_test::SamePinnedObject(evaluation.instance, instance) && evaluation.sceneEpoch == view.sceneEpoch &&
              evaluation.viewRevision == view.viewRevision && evaluation.geometryRevision == view.geometryRevision,
          "Sampled batch retains its exact count and view/material identity at the Scene boundary");
    double endpointInterpolationError = 0;
    for (unsigned i = 0; i < kSamples; ++i)
    {
        const auto actual = std::bit_cast<std::array<float, 20>>(actualPoints[i]);
        const auto reference = std::bit_cast<std::array<float, 20>>(expected[i]);
        for (unsigned c = 0; c < 20; ++c)
        {
            Near(actual[c], reference[c], "Perspective sample scalar reference");
        }
        auto material = ExpectedPoint(expected[i], view, 1.3f, .5f, tier != 0);
        const auto values = std::bit_cast<std::array<float, 44>>(actualMaterial[i]);
        const auto expectedValues = std::bit_cast<std::array<float, 44>>(material);
        for (unsigned c = 0; c < 44; ++c)
        {
            if (c < 3)
            {
                const double difference = std::abs(values[c] - expectedValues[c]);
                Check(difference <= .001, "Triangle SRGB texel selection");
                maxColorError = std::max(maxColorError, difference);
                material.baseAlpha[c] = values[c];
                ++gpuComponents;
            }
            else
            {
                Near(values[c], expectedValues[c], "Triangle graph scalar reference");
            }
        }
        const auto bakeValues = std::bit_cast<std::array<float, 36>>(ExpectedBake(material, environmentColors, table));
        const auto actualBakeValues = std::bit_cast<std::array<float, 36>>(actualBake[i]);
        for (unsigned c = 0; c < 36; ++c)
        {
            Near(actualBakeValues[c], bakeValues[c], "Triangle exact IBL scalar reference");
        }
        std::array<double, 3> mixed{};
        const auto& request = requests[i];
        const std::array<double, 3> weights{1.0 - request.b - request.c, request.b, request.c};
        for (unsigned corner = 0; corner < 3; ++corner)
        {
            auto endpoint = WorldReference(geometry, geometry.indices[std::size_t(request.triangle) * 3 + corner]);
            endpoint.uvLod[3] = request.lod;
            const auto endpointMaterial = ExpectedPoint(endpoint, view, 1.3f, .5f, tier != 0);
            for (unsigned c = 0; c < 3; ++c)
            {
                mixed[c] += weights[corner] * endpointMaterial.baseAlpha[c];
            }
        }
        for (unsigned c = 0; c < 3; ++c)
        {
            endpointInterpolationError = std::max(endpointInterpolationError, std::abs(mixed[c] - values[c]));
        }
    }
    Check(endpointInterpolationError > .05, "Sharp texture fixture detects material-at-vertex interpolation errors");
    std::string messages;
    Check(device.DrainDebugMessages(messages) == 0, "Triangle GPU validation: " + messages);
    Check(device.BeginFrame(error), "Sample reuse begin");
    Check(meshEvaluator.RecordSamples(device, vertices, requests, sampled, error),
          "Accepted vertex source across recordings");
    device.AbortFrame();
    if (tier == 1)
    {
        auto cancelled = GeometryFixture(assets::kCoreVertexAttributes, 0);
        Attribute(cancelled, 0, assets::VertexAttribute::Tangent, std::array<float, 4>{1, .12f, 0, 1});
        Attribute(cancelled, 1, assets::VertexAttribute::Tangent, std::array<float, 4>{1, .12f, 0, -1});
        Check(MeshSurfaceInput::Seal(cancelled.draw, view, cancelled.lods, input, error), "Cancelling tangent input");
        Check(device.BeginFrame(error), "Cancelling frame begin");
        textures.BeginFrame(++frames);
        Check(bindings.Prepare(device, textures, instance, evaluator.Layout(), materialBindings, error),
              "Cancelling bindings");
        Check(meshEvaluator.Record(device, input, vertices, error), "Cancelling vertices");
        const MeshSurfaceSample request{0, .5f, 0, 0};
        Check(meshEvaluator.RecordSamples(device, vertices, {&request, 1}, sampled, error),
              "Cancelling sample records");
        Check(evaluator.RecordGpu(device, materialBindings, sampled, surface, error), "Cancelling graph records");
        Check(baker.RecordGpu(device, environment, surface, bake, error), "Cancelling bake records");
        const RHIBufferHandle rejectedBuffers[]{vertices->Buffer(), sampled->Buffer(), surface->Buffer(),
                                                bake->Buffer()};
        const std::array<std::uint64_t, 4> bytes{kPoints * sizeof(SurfacePoint), sizeof(SurfacePoint),
                                                 sizeof(IblBakePoint), sizeof(IblBakeSample)};
        auto& rejectedEncoder = device.GetImmediateEncoder();
        for (unsigned i = 0; i < 4; ++i)
        {
            const RHIBufferTransition before{rejectedBuffers[i], RHIResourceState::ShaderResource,
                                             RHIResourceState::CopySource};
            rejectedEncoder.ResourceBarriers({{}, {&before, 1}});
            rejectedEncoder.CopyBufferToReadback(readbacks[i], rejectedBuffers[i], 0, bytes[i]);
        }
        Check(device.EndFrame(error), "Cancelling submit");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Cancelling native submit");
        device.WaitForGpu();
        for (unsigned i = 0; i < 4; ++i)
        {
            Check(device.MapReadback(readbacks[i], mapped[i], error), "Cancelling map");
        }
        Check(vertices->ValidateReadback({mapped[0].Elements<SurfacePoint>(), kPoints}, error),
              "Cancelling vertices still valid");
        Check(!sampled->ValidateReadback({mapped[1].Elements<SurfacePoint>(), 1}, error) && !sampled->IsValidated() &&
                  error.find("sample 0") != std::string::npos,
              "Interpolation with a zero bitangent rejects, never silently repairs a seam");
        Check(!surface->ValidateReadback({mapped[2].Elements<IblBakePoint>(), 1}, error) &&
                  mapped[3].Elements<IblBakeSample>()[0].baseAverage[3] == -1,
              "Invalid interpolated frame cannot enter accepted material or lookup");
        Check(device.DrainDebugMessages(messages) == 0, "Cancelling frame GPU validation: " + messages);
    }
    for (auto& readback : readbacks)
    {
        device.ReleaseReadback(readback);
    }
}

void Run(const std::filesystem::path& root)
{
    auto* paths = InternalPath::GetInstance();
    paths->BaseProjectPath = root / "Dynamic_CPP";
    paths->ShaderSourcePath = root / "Dynamic_CPP/Assets/Shaders";
    paths->AssetAuthoringEnabled = true;
    const std::array products{Product(root, false), Product(root, true)};
    const auto table = LoadSheenTable(root / "Tools/blender/fixtures/principled-layered-5.1.1/sheen-ltc.csv");
    const Environment environmentColors{{{2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}, {2, 1, .5}}};
    const auto image = Image(), cube = Cube(environmentColors);
    GenerationStore store;
    experiment::AssetId graph;
    Check(Uuid::TryParse("11111111-1111-4111-8111-111111111111", graph.value), "Graph GUID");
    std::string error;
    std::array<own::shared_owner<const Instance>, 2> instances;
    for (unsigned i = 0; i < 2; ++i)
    {
        const auto generation = store.Load(
            graph,
            [&](CookedProgram& cooked, std::string&) {
                cooked = {products[i], WriteMaterialProgramMetadata(products[i].program),
                          BuildBoundSource(products[i].program)};
                return true;
            },
            true, error);
        Check(generation && BuildInstance(
                                generation, {graph, {}, {}},
                                [&](const experiment::AssetId&, LXColorSpace, std::string&) { return image; },
                                instances[i], error),
              "Instance " + error);
    }
    RecordingChangeDevice device;
    Check(device.Initialize(64, 64, error), "Native device: " + error);
    {
        Check(device.BeginFrame(error), "Culling fixture begin");
        {
            EnhancedRenderGraph cullGraph(device);
            RGTextureDesc desc;
            desc.width = desc.height = 1;
            desc.allowRenderTarget = true;
            const auto used = cullGraph.CreateTexture(desc), unused = cullGraph.CreateTexture(desc);
            cullGraph.AddPass("producer", {{used, RHIResourceState::RenderTarget}}, [](const auto&) {});
            cullGraph.AddPass("discarded", {{unused, RHIResourceState::RenderTarget}}, [](const auto&) {});
            cullGraph.AddPass("root", {{used, RHIResourceState::ShaderResource},
                                  {used, RHIResourceState::ShaderResource}}, [](const auto&) {}, true);
            cullGraph.AddPass("later-producer", {{used, RHIResourceState::RenderTarget}}, [](const auto&) {});
            Check(cullGraph.Compile(error) && cullGraph.GetStats().passesExecuted == 3 &&
                      cullGraph.GetStats().passesCulled == 1,
                  "Indexed culling retains every writer and duplicate reads without retaining unrelated work");
            cullGraph.Reset();
            Check(cullGraph.Compile(error) && cullGraph.GetStats().passesExecuted == 0,
                  "Reset removes old writer reachability");
        }
        device.AbortFrame();
    }
    DX12RootSignatureCache roots;
    DX12PSOManager pipelines;
    DX12TextureCache textures;
    Check(roots.Initialize(&device, error), "Roots");
    Check(pipelines.Initialize(&device, L"", error), "Pipelines");
    Check(textures.Initialize(&device, error), "Textures");
    MeshSurfaceEvaluator meshEvaluator;
    IblBaker baker;
    const auto meshFile = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/MaterialGraphMeshSurface.slang";
    RHIShaderBlob transform, sampler, vs, ps, bake;
    for (auto backend : {RHIShaderBinary::SpirV, RHIShaderBinary::Dxil})
    {
        RHIShaderCompiler::ScopedOutput output(backend);
        Check(RHIShaderCompiler::CompileFile(meshFile.string(), "LXTransformMesh", "cs_6_0", transform, error),
              "Mesh CS: " + error);
        Check(RHIShaderCompiler::CompileFile(meshFile.string(), "LXSampleMesh", "cs_6_0", sampler, error),
              "Mesh sample CS: " + error);
        Check(RHIShaderCompiler::CompileFile(meshFile.string(), "LXMeshVS", "vs_6_0", vs, error), "Mesh VS: " + error);
        Check(RHIShaderCompiler::CompileFile(meshFile.string(), "LXMeshPS", "ps_6_0", ps, error), "Mesh PS: " + error);
        Check(RHIShaderCompiler::CompileFile(
                  (root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/PrincipledIblBake.slang").string(), "CSMain",
                  "cs_6_0", bake, error),
              "Bake CS: " + error);
    }
    Check(meshEvaluator.Initialize(device, roots, pipelines, transform, error), "Mesh evaluator " + error);
    Check(meshEvaluator.InitializeSampler(device, roots, pipelines, sampler, error), "Mesh sampler " + error);
    Check(baker.Initialize(device, roots, pipelines, bake, error), "Baker");
    std::array<SurfaceEvaluator, 2> evaluators;
    std::array<ScenePassLayout, 2> sceneLayouts;
    SceneMaterialSlot slot;
    Check(slot.Initialize(device, error), "Scene submission owner");
    RenderBindingCache bindings;
    for (unsigned i = 0; i < 2; ++i)
    {
        Check(evaluators[i].Initialize(device, roots, pipelines, products[i], RHIShaderBinary::Dxil, {}, error),
              "Graph evaluator");
        const RHIPipelineLayoutParam host[]{RHILayout::Cbv(0), RHILayout::Srv(0), RHILayout::Srv(2)};
        Check(CreateScenePassLayout(roots, products[i].layout, host, {}, false, sceneLayouts[i], error, 1),
              "Scene GPU source host with IBL t1");
    }
    const RHIPipelineLayoutParam meshHost[]{RHILayout::Cbv(1, RHIShaderVisibility::Vertex),
                                            RHILayout::Srv(3, RHIShaderVisibility::Vertex)};
    const auto meshLayout = roots.GetOrCreate({meshHost, {}, true}, error);
    RHIGraphicsPipelineDesc description;
    description.layout = meshLayout;
    description.vsBytecode = vs.Data();
    description.vsSize = vs.Size();
    description.psBytecode = ps.Data();
    description.psSize = ps.Size();
    description.rtvFormats[0] = RHIFormat::RGBA32Float;
    RHIGraphicsPipelineRequest graphics;
    Check(graphics.Create(pipelines, description, error), "Indexed mesh pipeline " + error);
    RHITextureDesc targetDescription;
    targetDescription.width = targetDescription.height = 64;
    targetDescription.allowRenderTarget = true;
    targetDescription.format = RHIFormat::RGBA32Float;
    RHITextureHandle target;
    Check(device.CreateTexture(targetDescription, target, error), "Mesh target");
    std::array<RHIReadback, 5> readbacks;
    Check(device.CreateBufferReadback(kPoints * sizeof(SurfacePoint), readbacks[0], error), "World readback");
    Check(device.CreateBufferReadback(kPoints * sizeof(IblBakePoint), readbacks[1], error), "Material readback");
    Check(device.CreateBufferReadback(kPoints * sizeof(IblBakeSample), readbacks[2], error), "IBL readback");
    Check(device.CreateReadback(64, 64, RHIFormat::RGBA32Float, 1, readbacks[3], error), "Indexed mesh readback");
    Check(device.CreateReadback(64, 64, RHIFormat::RGBA32Float, 1, readbacks[4], error),
          "Scene lookup consumer readback");
    RHIResourceState targetState = RHIResourceState::Common;
    SurfaceView view{{.1f, .2f, 2, 0}, 1, 1, 1};
    unsigned frames = 0;
    {
        Drain drain{device};
        const auto partition = PartitionTests(view);
        VerifyPartitionGpu(device, meshEvaluator, *partition);
        auto rasterGeometry = LargeGeometry(assets::kModelVertexMasks.back());
        MeshSurfacePlanBudget rasterBudget;
        rasterBudget.maxChunkPoints = rasterBudget.maxSourceVertices;
        std::shared_ptr<const MeshSurfacePlan> rasterPlan;
        Check(MeshSurfacePlan::BuildForScene(rasterGeometry.draw, view, rasterBudget, rasterPlan, error) &&
                  rasterPlan->Chunks().size() == 1,
              "Large raster mesh prepares one draw");
        VerifyPartitionGpu(device, meshEvaluator, *rasterPlan, true);
        VerifyResidentInputsGpu(device, meshEvaluator, view);
        VerifyCacheAdmissionGpu(device, meshEvaluator, view);
        for (unsigned mask : assets::kModelVertexMasks)
        {
            for (unsigned pose = 0; pose < 3; ++pose)
            {
                const bool layered = pose != 0;
                const unsigned tier = layered ? 1 : 0;
                view.geometryRevision = frames + 1;
                view.viewRevision = frames + 1;
                view.eye[0] = pose == 1 ? .4f : .1f;
                auto geometry = GeometryFixture(mask, pose);
                std::shared_ptr<const MeshSurfaceInput> sealed;
                Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, sealed, error), "Seal " + error);
                std::shared_ptr<const MeshSurfaceInput> same;
                Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, same, error) && sealed->Matches(*same),
                      "Exact sealed identity");
                auto accepted = sealed;
                auto badDraw = geometry.draw;
                badDraw.modelMeshView.vertexStride += 4;
                Check(!MeshSurfaceInput::Seal(badDraw, view, geometry.lods, sealed, error) && sealed == accepted,
                      "Wrong stride preserves source");
                badDraw = geometry.draw;
                badDraw.worldMatrix.m[0][3] = 1;
                Check(!MeshSurfaceInput::Seal(badDraw, view, geometry.lods, sealed, error) && sealed == accepted,
                      "Nonaffine transform preserves source");
                auto badLods = geometry.lods;
                badLods[0] = -1;
                Check(!MeshSurfaceInput::Seal(geometry.draw, view, badLods, sealed, error) && sealed == accepted,
                      "Missing LOD preserves source");
                badLods = geometry.lods;
                badLods[0] = geometry.lods[0] == 0 ? 1.f : 0.f;
                Check(MeshSurfaceInput::Seal(geometry.draw, view, badLods, same, error) && !sealed->Matches(*same),
                      "LOD change rejects reuse");
                auto otherView = view;
                ++otherView.viewRevision;
                Check(MeshSurfaceInput::Seal(geometry.draw, otherView, geometry.lods, same, error) &&
                          !sealed->Matches(*same),
                      "View change rejects reuse");
                const auto firstIndex = geometry.indices[0];
                geometry.indices[0] = kPoints;
                Check(!MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, same, error),
                      "Out of range triangle is rejected");
                geometry.indices[0] = firstIndex;
                if (!geometry.bones.empty())
                {
                    Attribute(geometry, 1, assets::VertexAttribute::BoneIndices,
                              std::array<std::uint8_t, 4>{255, 1, 0, 1});
                    Check(!MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, same, error),
                          "Out of range palette read is rejected before GPU recording");
                    Attribute(geometry, 1, assets::VertexAttribute::BoneIndices,
                              std::array<std::uint8_t, 4>{0, 255, 255, 255});
                    Attribute(geometry, 1, assets::VertexAttribute::BoneWeights,
                              std::array<float, 4>{.25f, .85f, 0, 0});
                    Check(!MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, same, error),
                          "Invalid weighted affine pose is rejected");
                    Attribute(geometry, 1, assets::VertexAttribute::BoneWeights, std::array<float, 4>{1, 0, 0, 0});
                    Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, same, error),
                          "Zero-weight sentinel indices do not reference the palette");
                }
                std::vector<SurfacePoint> expected;
                for (unsigned i = 0; i < kPoints; ++i)
                {
                    expected.push_back(WorldReference(geometry, i));
                }
                // Mutate the producer after sealing. The render input owns its pose/bytes.
                geometry.vertices[0] = std::byte{0xff};
                if (!geometry.bones.empty())
                {
                    geometry.bones[0].m[3][1] = 99;
                }
                Check(device.BeginFrame(error), "Begin");
                textures.BeginFrame(frames);
                std::shared_ptr<const RenderBindings> materialBindings;
                Check(bindings.Prepare(device, textures, instances[tier], evaluators[tier].Layout(), materialBindings,
                                       error),
                      "Material bind prepare");
                const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), error), 1, cube};
                auto& encoder = device.GetImmediateEncoder();
                if (frames == 0)
                {
                    const auto texture = textures.GetOrUpload((image ? &*image.borrow() : nullptr), error);
                    const RHITransition transitions[]{
                        {texture.handle, RHIResourceState::PixelShaderResource, RHIResourceState::ShaderResource},
                        {environment.cube.handle, RHIResourceState::PixelShaderResource,
                         RHIResourceState::ShaderResource}};
                    encoder.ResourceBarriers({transitions});
                }
                std::shared_ptr<const MeshSurfaceBatch> mesh;
                std::shared_ptr<const SurfaceBatch> surface;
                std::shared_ptr<const IblBakeResult> result;
                std::shared_ptr<const SceneMaterialPacket> packet;
                Check(meshEvaluator.Record(device, sealed, mesh, error), "GPU skin/world " + error);
                Check(evaluators[tier].RecordGpu(device, materialBindings, mesh, surface, error),
                      "GPU world -> graph " + error);
                SceneSurfaceEvaluation evaluation;
                Check(BuildSceneSurfaceEvaluation(surface, evaluation, error), "Owning Scene GPU evaluation");
                const auto preparePacket = [&](const SceneSurfaceEvaluation& source) {
                    RHIGraphicsPipelineDesc pipeline;
                    pipeline.layout = sceneLayouts[tier].material.handle;
                    pipeline.rtvFormats[0] = RHIFormat::RGBA32Float;
                    EnhancedMaterialCoverage coverage;
                    coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
                    return slot.Prepare(textures, pipelines, bindings, baker, source, environment, coverage,
                                        sceneLayouts[tier], pipeline, RHIShaderBinary::Dxil,
                                        {.coreForward = true, .layeredLookup = true}, {}, packet, error);
                };
                Check(preparePacket(evaluation), "Scene GPU graph -> bake " + error);
                const auto acceptedPacket = packet;
                auto wrongEvaluation = evaluation;
                ++wrongEvaluation.viewRevision;
                Check(!preparePacket(wrongEvaluation) && packet == acceptedPacket,
                      "Scene source identity mismatch preserves packet");
                wrongEvaluation = evaluation;
                wrongEvaluation.points.push_back({});
                Check(!preparePacket(wrongEvaluation) && packet == acceptedPacket,
                      "Ambiguous CPU/GPU evaluation preserves packet");
                result = packet->ibl;
                Check(surface->Matches(*instances[tier], *mesh), "Exact GPU source identity");
                const RHIBufferHandle buffers[]{mesh->Buffer(), surface->Buffer(), result->Buffer()};
                for (unsigned i = 0; i < 3; ++i)
                {
                    const RHIBufferTransition before{buffers[i], RHIResourceState::ShaderResource,
                                                     RHIResourceState::CopySource};
                    encoder.ResourceBarriers({{}, {&before, 1}});
                    encoder.CopyBufferToReadback(readbacks[i], buffers[i]);
                    const RHIBufferTransition after{buffers[i], RHIResourceState::CopySource,
                                                    RHIResourceState::ShaderResource};
                    encoder.ResourceBarriers({{}, {&after, 1}});
                }
                const RHITransition before{target, targetState, RHIResourceState::RenderTarget};
                encoder.ResourceBarriers({{&before, 1}});
                const auto rtv = device.CreateRenderTargets({&target, 1});
                Check(rtv.IsValid(), "Mesh RTV");
                encoder.SetPipeline(RHIBindPoint::Graphics, graphics.GetHandle());
                const auto projection = math::matrix4x4::identity();
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                          device.UploadConstants(&projection, sizeof(projection)));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(mesh->Buffer()));
                const auto indexUpload = device.AllocateUpload(
                    {sealed->Geometry().indexCount * sizeof(std::uint32_t), RHIUploadUsage::IndexData, 4});
                Check(indexUpload.IsWritable(), "Index upload");
                std::memcpy(indexUpload.cpuAddress, sealed->Geometry().indexData,
                            sealed->Geometry().indexCount * sizeof(std::uint32_t));
                encoder.SetIndexBuffer(indexUpload, RHIFormat::R32Uint);
                encoder.BindRenderTargets(rtv);
                const float clear[]{0, 0, 0, 0};
                encoder.ClearRenderTargets(rtv, clear);
                encoder.SetViewportAndScissor(64, 64);
                encoder.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                encoder.DrawIndexed(sealed->Geometry().indexCount, 1);
                const RHITransition after{target, RHIResourceState::RenderTarget, RHIResourceState::CopySource};
                encoder.ResourceBarriers({{&after, 1}});
                encoder.CopyToReadback(readbacks[3], target);
                const RHITransition lookupBefore{target, RHIResourceState::CopySource, RHIResourceState::RenderTarget};
                encoder.ResourceBarriers({{&lookupBefore, 1}});
                Check(slot.Bind(encoder, *packet, error), "GPU Scene packet graphics bind");
                struct SurfaceConstants
                {
                    std::array<std::uint32_t, 4> countTier;
                    IblVector eye;
                };
                // This field selects the IBL model, not the Core/Layered product index.
                // Match the model used by SurfaceEvaluator and ExpectedPoint for both graphs.
                const SurfaceConstants constants{{kPoints, 1, 0, 0}, view.eye};
                encoder.SetConstantBuffer(RHIBindPoint::Graphics, 0,
                                          device.UploadConstants(&constants, sizeof(constants)));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 1, RHIBufferSlice::Whole(mesh->Buffer()));
                encoder.SetRootBuffer(RHIBindPoint::Graphics, 2, RHIBufferSlice::Whole(surface->Buffer()));
                encoder.BindRenderTargets(rtv);
                encoder.ClearRenderTargets(rtv, clear);
                encoder.Draw(3, 1);
                encoder.ResourceBarriers({{&after, 1}});
                encoder.CopyToReadback(readbacks[4], target);
                targetState = RHIResourceState::CopySource;
                const auto recording = device.GetCurrentUploadRecordingId();
                const auto lastGood = slot.Active();
                Check(device.EndFrame(error), "Submit");
                Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Native submit");
                const RHICompletionPoint submitted{device.GetLastSignaledFenceValue()};
                Check(!slot.PublishSubmitted(recording, submitted, error) && slot.Active() == lastGood,
                      "Submission alone does not publish an unvalidated GPU evaluation");
                device.WaitForGpu();
                std::array<RHIReadbackImage, 5> mapped;
                for (unsigned i = 0; i < 5; ++i)
                {
                    Check(device.MapReadback(readbacks[i], mapped[i], error), "Readback");
                }
                const auto worldPoints = mapped[0].Elements<SurfacePoint>();
                const auto materialPoints = mapped[1].Elements<IblBakePoint>();
                const auto samples = mapped[2].Elements<IblBakeSample>();
                Check(!surface->ValidateReadback({materialPoints, kPoints}, error),
                      "Graph acceptance requires accepted geometry first");
                Check(mesh->ValidateReadback({worldPoints, kPoints}, error), "Geometry acceptance " + error);
                Check(surface->ValidateReadback({materialPoints, kPoints}, error), "Graph acceptance " + error);
                Check(slot.PublishSubmitted(recording, submitted, error) && slot.Active() == packet,
                      "Completed accepted GPU evaluation publishes owning packet");
                for (unsigned i = 0; i < kPoints; ++i)
                {
                    const auto expectedWorld = std::bit_cast<std::array<float, 20>>(expected[i]);
                    const auto actualWorld = std::bit_cast<std::array<float, 20>>(worldPoints[i]);
                    for (unsigned c = 0; c < 20; ++c)
                    {
                        Near(actualWorld[c], expectedWorld[c], "World frame scalar reference");
                    }
                    auto expectedMaterial = ExpectedPoint(expected[i], view, 1.3f, .5f, layered);
                    const auto actualMaterial = std::bit_cast<std::array<float, 44>>(materialPoints[i]);
                    auto materialValues = std::bit_cast<std::array<float, 44>>(expectedMaterial);
                    for (unsigned c = 0; c < 44; ++c)
                    {
                        if (c < 3)
                        {
                            const double difference = std::abs(actualMaterial[c] - materialValues[c]);
                            maxColorError = std::max(maxColorError, difference);
                            Check(difference <= .001, "SRGB source tolerance");
                            expectedMaterial.baseAlpha[c] = actualMaterial[c];
                            ++gpuComponents;
                        }
                        else
                        {
                            Near(actualMaterial[c], materialValues[c], "Mesh graph input");
                        }
                    }
                    const auto expectedBake = ExpectedBake(expectedMaterial, environmentColors, table);
                    const auto actualBake = std::bit_cast<std::array<float, 36>>(samples[i]);
                    for (unsigned c = 0; c < 36; ++c)
                    {
                        Near(actualBake[c], std::bit_cast<std::array<float, 36>>(expectedBake)[c],
                             "Mesh IBL reference");
                    }
                    const auto expectedPixel = Ambient(expectedMaterial, expectedBake, table);
                    for (unsigned c = 0; c < 3; ++c)
                    {
                        Near(mapped[4].At(i, 0, c), expectedPixel[c], "Mesh source Scene lookup draw");
                    }
                    Near(mapped[4].At(i, 0, 3), 1, "Lookup draw alpha");
                }
                unsigned covered = 0;
                for (unsigned y = 0; y < 64; ++y)
                {
                    for (unsigned x = 0; x < 64; ++x)
                    {
                        if (mapped[3].At(x, y, 3) < .5f)
                        {
                            continue;
                        }
                        ++covered;
                        Near(mapped[3].At(x, y, 0), (double(x) + .5) / 32 - 1, "Raster world x");
                        Near(mapped[3].At(x, y, 1), 1 - (double(y) + .5) / 32, "Raster world y");
                    }
                }
                Check(covered > 150, "Real indexed mesh coverage");
                std::string messages;
                Check(device.DrainDebugMessages(messages) == 0, "GPU validation: " + messages);
                // A validated world source is reusable with fresh recording bindings.
                Check(device.BeginFrame(error), "Reuse begin");
                textures.BeginFrame(++frames);
                Check(bindings.Prepare(device, textures, instances[tier], evaluators[tier].Layout(), materialBindings,
                                       error),
                      "Reuse bindings");
                Check(evaluators[tier].RecordGpu(device, materialBindings, mesh, surface, error),
                      "Validated source across recordings");
                device.AbortFrame();
            }
        }
        for (unsigned tier = 0; tier < 2; ++tier)
        {
            VerifySamplesGpu(device, textures, bindings, meshEvaluator, evaluators[tier], baker, instances[tier], cube,
                             environmentColors, table, view, tier, frames);
        }
        auto geometry = GeometryFixture(assets::kCoreVertexAttributes, 0);
        std::shared_ptr<const MeshSurfaceInput> sealed;
        Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, sealed, error), "Abort source seal");
        Check(device.BeginFrame(error), "Mesh abort begin");
        std::shared_ptr<const MeshSurfaceBatch> aborted;
        Check(meshEvaluator.Record(device, sealed, aborted, error), "Unsubmitted mesh transform");
        device.AbortFrame();
        Check(device.BeginFrame(error), "Mesh abort reuse begin");
        textures.BeginFrame(++frames);
        std::shared_ptr<const RenderBindings> materialBindings;
        Check(bindings.Prepare(device, textures, instances[0], evaluators[0].Layout(), materialBindings, error),
              "Abort material bindings");
        std::shared_ptr<const SurfaceBatch> rejected;
        Check(!evaluators[0].RecordGpu(device, materialBindings, aborted, rejected, error) && !rejected,
              "Aborted unvalidated geometry cannot cross recordings");
        device.AbortFrame();
        aborted.reset();

        // A missing tangent basis is valid. A zero view direction is not.
        // Put vertex 0 exactly at the eye after an accepted affine transform.
        geometry.draw.worldMatrix = math::matrix4x4::identity();
        geometry.draw.worldMatrix.m[3][0] = view.eye[0];
        geometry.draw.worldMatrix.m[3][1] = view.eye[1];
        geometry.draw.worldMatrix.m[3][2] = view.eye[2] - .2f;
        Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, sealed, error),
              "Affine eye-coincident source seal");
        Check(device.BeginFrame(error), "Invalid transformed frame begin");
        textures.BeginFrame(++frames);
        Check(bindings.Prepare(device, textures, instances[0], evaluators[0].Layout(), materialBindings, error),
              "Invalid frame material bindings");
        std::shared_ptr<const MeshSurfaceBatch> collapsed;
        Check(meshEvaluator.Record(device, sealed, collapsed, error), "Record eye-coincident transform");
        Check(evaluators[0].RecordGpu(device, materialBindings, collapsed, rejected, error),
              "Record invalid frame graph candidate");
        SceneSurfaceEvaluation invalidEvaluation;
        Check(BuildSceneSurfaceEvaluation(rejected, invalidEvaluation, error), "Invalid GPU candidate ownership");
        const IblEnvironment environment{textures.GetOrUpload((cube ? &*cube.borrow() : nullptr), error), 1, cube};
        EnhancedMaterialCoverage coverage;
        coverage.flags = EnhancedMaterialCoverage::Enabled | EnhancedMaterialCoverage::DoubleSided;
        RHIGraphicsPipelineDesc invalidPipeline;
        invalidPipeline.layout = sceneLayouts[0].material.handle;
        invalidPipeline.rtvFormats[0] = RHIFormat::RGBA32Float;
        std::shared_ptr<const SceneMaterialPacket> invalidPacket;
        Check(slot.Prepare(textures, pipelines, bindings, baker, invalidEvaluation, environment, coverage,
                           sceneLayouts[0], invalidPipeline, RHIShaderBinary::Dxil,
                           {.coreForward = true, .layeredLookup = true}, {}, invalidPacket, error),
              "Invalid GPU candidate records without publication");
        auto& encoder = device.GetImmediateEncoder();
        const RHIBufferHandle buffers[]{collapsed->Buffer(), rejected->Buffer()};
        for (unsigned i = 0; i < 2; ++i)
        {
            const RHIBufferTransition before{buffers[i], RHIResourceState::ShaderResource,
                                             RHIResourceState::CopySource};
            encoder.ResourceBarriers({{}, {&before, 1}});
            encoder.CopyBufferToReadback(readbacks[i], buffers[i]);
        }
        const auto recording = device.GetCurrentUploadRecordingId();
        const auto lastGood = slot.Active();
        Check(device.EndFrame(error), "Submit invalid transformed frame");
        Check(GetRHISubmissionThread().DrainSubmissions(&device, error), "Native invalid candidate submit");
        const RHICompletionPoint submitted{device.GetLastSignaledFenceValue()};
        device.WaitForGpu();
        std::array<RHIReadbackImage, 2> mapped;
        for (unsigned i = 0; i < 2; ++i)
        {
            Check(device.MapReadback(readbacks[i], mapped[i], error), "Invalid frame readback");
        }
        Check(!collapsed->ValidateReadback({mapped[0].Elements<SurfacePoint>(), kPoints}, error) &&
                  error.find("vertex 0") != std::string::npos,
              "GPU geometry failure identifies the vertex");
        Check(!rejected->ValidateReadback({mapped[1].Elements<IblBakePoint>(), kPoints}, error),
              "Invalid source cannot be accepted downstream");
        Check(!slot.PublishSubmitted(recording, submitted, error) && slot.Active() == lastGood,
              "Completed invalid geometry cannot replace the last good Scene packet");
        slot.RejectSubmitted(recording);
        Check(slot.Active() == lastGood && slot.RetainedRecordingCount() == 0,
              "Rejected completed candidate releases submission owners");
        std::string messages;
        Check(device.DrainDebugMessages(messages) == 0, "GPU failure-path validation: " + messages);
        geometry = GeometryFixture(assets::kCoreVertexAttributes, 0);
        Check(MeshSurfaceInput::Seal(geometry.draw, view, geometry.lods, sealed, error), "Prefix source seal");
        Check(device.BeginFrame(error), "Prefix transform begin");
        std::shared_ptr<const MeshSurfaceBatch> prefixMesh;
        Check(meshEvaluator.Record(device, sealed, prefixMesh, error), "Prefix retained mesh");
        auto preservedMesh = prefixMesh;
        device.flushOnUpload = true;
        Check(!meshEvaluator.Record(device, sealed, prefixMesh, error) && prefixMesh == preservedMesh,
              "Allocation prefix submission cannot mix mesh upload recordings");
        device.AbortFrame();
        device.WaitForGpu();
        Check(device.BeginFrame(error), "Prefix bake begin");
        textures.BeginFrame(++frames);
        Check(bindings.Prepare(device, textures, instances[0], evaluators[0].Layout(), materialBindings, error),
              "Prefix graph bindings");
        Check(meshEvaluator.Record(device, sealed, prefixMesh, error), "Prefix bake mesh");
        Check(evaluators[0].RecordGpu(device, materialBindings, prefixMesh, rejected, error), "Prefix graph source");
        auto preservedBake = slot.Active()->ibl;
        auto bakeCandidate = preservedBake;
        device.flushOnUpload = true;
        Check(!baker.RecordGpu(device, environment, rejected, bakeCandidate, error) && bakeCandidate == preservedBake,
              "Allocation prefix submission cannot mix bake descriptor/upload recordings");
        device.AbortFrame();
        device.WaitForGpu();
        Check(device.DrainDebugMessages(messages) == 0, "Native prefix failure-path validation: " + messages);
        Check(device.BeginFrame(error), "Prefix sampling begin");
        Check(meshEvaluator.Record(device, sealed, prefixMesh, error), "Prefix sampling vertices");
        const auto requests = SampleRequests();
        std::shared_ptr<const MeshSurfaceBatch> prefixSamples;
        Check(meshEvaluator.RecordSamples(device, prefixMesh, requests, prefixSamples, error),
              "Prefix retained samples");
        const auto preservedSamples = prefixSamples;
        device.flushOnUpload = true;
        Check(!meshEvaluator.RecordSamples(device, prefixMesh, requests, prefixSamples, error) &&
                  prefixSamples == preservedSamples,
              "Native prefix cannot mix triangle sampling upload/descriptor recordings");
        device.AbortFrame();
        device.WaitForGpu();
        Check(device.DrainDebugMessages(messages) == 0, "Sampling prefix GPU validation: " + messages);
    }
    slot.ShutdownAfterIdle();
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
    std::cout << "LX_MATERIAL_MESH_SURFACE_OK checks=" << checks << " gpuComponents=" << gpuComponents
              << " masks=8 poses=3 points=37 submitted=32 compiled=22 partitionVertices=8200 samplePoints=82 "
                 "maxNormalizedError="
              << maxError << " maxSrgbError=" << maxColorError << '\n';
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
        std::cerr << "LX_MATERIAL_MESH_SURFACE_FAILED " << error.what() << '\n';
        return 1;
    }
}
