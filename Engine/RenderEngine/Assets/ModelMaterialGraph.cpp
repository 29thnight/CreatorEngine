#include "ModelMaterialGraph.h"
#include "../StandardMaterialProperty.h"
#include "../../../Lattice/Material/LXMaterialCompiler.h"
#include "AuthoringParsedDocument.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace assets
{
namespace
{
using namespace LX;

std::string ReadText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

const experiment::MaterialPropertyValue* Property(const experiment::Material& material, std::string_view name)
{
    const auto found = std::ranges::find(material.properties, name, &experiment::MaterialProperty::name);
    return found == material.properties.end() ? nullptr : &found->value;
}

template<typename T>
T Value(const experiment::Material& material, std::string_view name, T fallback)
{
    const auto* property = Property(material, name);
    if (property)
    {
        if (const auto* value = std::get_if<T>(property))
            return *value;
        throw std::runtime_error("Imported material property has the wrong type: " + std::string(name));
    }
    return fallback;
}

std::string Sampler(const TextureSampler& sampler)
{
    const auto filter = [](RHIFilterMode mode) -> std::string {
        if (mode == RHIFilterMode::Linear)
            return "linear";
        if (mode == RHIFilterMode::Point)
            return "nearest";
        throw std::runtime_error("Unsupported imported texture filter");
    };
    const auto address = [](RHIAddressMode mode) -> std::string {
        if (mode == RHIAddressMode::Wrap)
            return "repeat";
        if (mode == RHIAddressMode::Clamp)
            return "clamp";
        if (mode == RHIAddressMode::Mirror)
            return "mirror";
        throw std::runtime_error("Unsupported imported texture addressing");
    };
    return filter(sampler.minMag) + "-" + filter(sampler.mip) + "-" + address(sampler.addressU) + "-" +
           address(sampler.addressV);
}

struct Builder
{
    const experiment::Material& material;
    LXMaterialAsset asset{};
    float row{};
    Id parameter{};

    Id Node(const std::string& type, float x, float y)
    {
        const Id result = asset.CreateNode(type, x, y);
        if (!result)
            throw std::runtime_error("Cannot create imported material node: " + type);
        return result;
    }

    Id Pin(Id node, const char* name, Direction direction = Direction::Input)
    {
        const auto& pins = asset.graph.FindNode(node)->pins;
        const auto found = std::ranges::find_if(
            pins, [&](const auto& pin) { return pin.Identifier() == name && pin.direction == direction; });
        if (found == pins.end())
            throw std::runtime_error("Missing imported material socket: " + std::string(name));
        return found->id;
    }

    void Connect(Id source, Id node, const char* input)
    {
        std::string error;
        if (!asset.graph.Connect(source, Pin(node, input), &error))
            throw std::runtime_error(error);
    }

    Id Parameter(const char* identifier, const char* label, PinType type, LXSocketValue value,
                 LXColorSpace colorSpace = LXColorSpace::Data)
    {
        const Id id = ++parameter;
        asset.blackboard.push_back({id, identifier, label, type, std::move(value), colorSpace, true});
        const Id node = asset.CreateNode(std::string("LXParameter") + PinTypeName(type), -960, row, label);
        if (!node)
        {
            throw std::runtime_error("Cannot create imported material parameter: " + std::string(label));
        }
        asset.graph.SetProperty(node, "parameter", std::to_string(id));
        asset.graph.SetProperty(node, "valueSource", "socket");
        asset.graph.SetSocketValue(Pin(node, "Value", Direction::Output), asset.blackboard.back().value);
        row += 110;
        return Pin(node, "Value", Direction::Output);
    }

    Id Scalar(const char* identifier, const char* label, float fallback)
    {
        return Parameter(identifier, label, PinType::Float, double(Value(material, identifier, fallback)));
    }

    std::optional<Id> Texture(const char* property, const char* label, LXColorSpace colorSpace)
    {
        const auto* value = Property(material, property);
        if (!value)
            return {};
        const auto* texture = std::get_if<experiment::TextureReference>(value);
        if (!texture || !texture->assetId.IsValid())
            throw std::runtime_error("Imported texture requires a stable asset GUID: " + std::string(property));
        if (!texture->coordinates.IsValid() || texture->coordinates.set != 0)
            throw std::runtime_error("Material graph Scene transport currently requires UV0: " + std::string(property));
        const float y = row;
        const Id resource =
            Parameter(property, label, PinType::Texture, Uuid::ToString(texture->assetId.value), colorSpace);
        const Id sample = Node("LXTextureSample", -630, y);
        Connect(resource, sample, "Texture");
        asset.graph.SetSocketValue(Pin(sample, "Sampler"), Sampler(texture->sampler));
        asset.graph.SetProperty(sample, "colorSpace", colorSpace == LXColorSpace::SRGB ? "srgb" : "data");
        const auto& uv = texture->coordinates;
        if (uv != TextureCoordinates{})
        {
            const Id coordinates = Node("LXTextureCoordinates", -960, row);
            asset.graph.SetSocketValue(Pin(coordinates, "Offset"),
                                       std::array<double, 3>{uv.offset[0], uv.offset[1], 0});
            asset.graph.SetSocketValue(Pin(coordinates, "Scale"), std::array<double, 3>{uv.scale[0], uv.scale[1], 1});
            asset.graph.SetSocketValue(Pin(coordinates, "Rotation"), double(uv.rotation));
            Connect(Pin(coordinates, "Vector", Direction::Output), sample, "Vector");
            row += 190;
        }
        row += 160;
        return sample;
    }

    Id Multiply(Id a, Id b, PinType type, float x, float y)
    {
        const Id node = Node(std::string("LXMultiply") + PinTypeName(type), x, y);
        Connect(a, node, "A");
        Connect(b, node, "B");
        return Pin(node, "Result", Direction::Output);
    }

    LXMaterialAsset Build()
    {
        const Id bsdf = Node("ShaderNodeBsdfPrincipled", 140, 0);
        const auto base = Value(material, "baseColor", math::vector4{1, 1, 1, 1});
        Id color = Parameter("baseColor", "Base Color", PinType::Color,
                             std::array<double, 4>{base.x, base.y, base.z, 1}, LXColorSpace::Linear);
        const auto baseMap = Texture("baseColorMap", "Base Color Texture", LXColorSpace::SRGB);
        if (baseMap)
            color = Multiply(color, Pin(*baseMap, "Color", Direction::Output), PinType::Color, -260, 0);
        Connect(color, bsdf, "Base Color");

        Id metallic = Scalar("metallic", "Metallic", 1);
        Id roughness = Scalar("roughness", "Roughness", 1);
        const auto orm = Texture("ormMap", "Metallic / Roughness Texture", LXColorSpace::Data);
        if (orm)
        {
            const Id channels = Node("ShaderNodeSeparateColor", -320, 420);
            Connect(Pin(*orm, "Color", Direction::Output), channels, "Color");
            roughness = Multiply(roughness, Pin(channels, "Green", Direction::Output), PinType::Float, -30, 200);
            metallic = Multiply(metallic, Pin(channels, "Blue", Direction::Output), PinType::Float, -30, 340);
        }
        Connect(metallic, bsdf, "Metallic");
        Connect(roughness, bsdf, "Roughness");
        if (const auto normal = Texture("normalMap", "Normal Texture", LXColorSpace::Data))
        {
            const Id node = Node("ShaderNodeNormalMap", -270, 650);
            Connect(Pin(*normal, "Color", Direction::Output), node, "Color");
            Connect(Scalar("normalScale", "Normal Strength", 1), node, "Strength");
            Connect(Pin(node, "Normal", Direction::Output), bsdf, "Normal");
        }
        const auto emissive = Value(material, "emissive", math::vector3{});
        Id emission = Parameter("emissive", "Emission Color", PinType::Color,
                                std::array<double, 4>{emissive.x, emissive.y, emissive.z, 1}, LXColorSpace::Linear);
        if (const auto texture = Texture("emissiveMap", "Emission Texture", LXColorSpace::SRGB))
            emission = Multiply(emission, Pin(*texture, "Color", Direction::Output), PinType::Color, -270, 930);
        Connect(emission, bsdf, "Emission Color");
        Connect(Scalar("emissiveStrength", "Emission Strength", 1), bsdf, "Emission Strength");

        const Id settings = Node("LXSurfaceSettings", 530, 40);
        Connect(Pin(bsdf, "BSDF", Direction::Output), settings, "Surface");
        const auto ao = Texture("aoMap", "Occlusion Texture", LXColorSpace::Data);
        if (ao)
        {
            const Id channels = Node("ShaderNodeSeparateColor", -320, 1190);
            Connect(Pin(*ao, "Color", Direction::Output), channels, "Color");
            Connect(Pin(channels, "Red", Direction::Output), settings, "Occlusion");
        }
        // glTF metallicRoughness R is undefined. It is not AO unless a
        // separately authored occlusion texture is present (which may share
        // the same image, but has an explicit aoMap binding).
        if (ao)
            Connect(Scalar("occlusionStrength", "Occlusion Strength", 1), settings, "Strength");
        const auto mode = material.blendMode;
        asset.graph.SetProperty(settings, "alphaMode",
                                mode == experiment::MaterialBlendMode::Transparent ? "transparent"
                                : mode == experiment::MaterialBlendMode::Masked    ? "masked"
                                                                                   : "opaque");
        if (mode != experiment::MaterialBlendMode::Opaque)
        {
            Id alpha = Parameter("alpha", "Alpha", PinType::Float, double(base.w));
            if (baseMap)
                alpha = Multiply(alpha, Pin(*baseMap, "Alpha", Direction::Output), PinType::Float, -260, 140);
            Connect(alpha, bsdf, "Alpha");
            if (mode == experiment::MaterialBlendMode::Masked)
                Connect(Scalar("alphaCutoff", "Alpha Cutoff", .5f), settings, "Alpha Cutoff");
        }
        asset.activeOutput = Node("ShaderNodeOutputMaterial", 870, 40);
        Connect(Pin(settings, "Surface", Direction::Output), asset.activeOutput, "Surface");
        return std::move(asset);
    }
};
} // namespace

Uuid::Uuid16 ModelMaterialGraphId(const Uuid::Uuid16& materialId)
{
    if (!IsUuidV8(materialId))
        return {};
    return DeriveIdentity({"model-material", materialId.data, "shadergraph", "source"}).uuid;
}

std::filesystem::path ModelMaterialGraphPath(const std::filesystem::path& assets, const Uuid::Uuid16& modelId,
                                             const Uuid::Uuid16& materialId)
{
    return assets / "Materials" / "Models" / Uuid::ToString(modelId) / (Uuid::ToString(materialId) + ".shadergraph");
}

bool ModelMaterialGraphsPresent(const std::filesystem::path& assets, const ModelAssetGeneration& model)
{
    for (const auto& material : model.Materials())
    {
        const auto path = ModelMaterialGraphPath(assets, model.Identity().modelId, material.materialId);
        if (!ModelMaterialGraphIdentityMatches(path, ModelMaterialGraphId(material.materialId)))
            return false;
    }
    return true;
}

bool ModelMaterialGraphIdentityMatches(const std::filesystem::path& path, const Uuid::Uuid16& graphId)
{
    if (!std::filesystem::is_regular_file(path) || !std::filesystem::is_regular_file(path.string() + ".meta"))
    {
        return false;
    }
    std::string error;
    const auto document = Authoring::ParsedDocument::ParseText(ReadText(path.string() + ".meta"), error);
    return document && document.Root()["guid"].AsString() == Uuid::ToString(graphId);
}

std::optional<LX::LXMaterialAsset> BuildModelMaterialGraph(const experiment::Material& material, std::string& error)
{
    try
    {
        auto graph = Builder{material}.Build();
        std::vector<LXMaterialDiagnostic> diagnostics;
        const auto program = GenerateMaterialSlang(graph, &diagnostics);
        if (!program)
        {
            error = diagnostics.empty() ? "Imported material graph generation failed" : diagnostics.front().message;
            return {};
        }
        return graph;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return {};
    }
}

ModelMaterialGraphPublication::~ModelMaterialGraphPublication()
{
    for (const auto& file : files_)
    {
        std::error_code ignored;
        std::filesystem::remove(file.staging, ignored);
        if (file.published && !committed_)
            std::filesystem::remove(file.destination, ignored);
    }
}

bool ModelMaterialGraphPublication::Prepare(const std::filesystem::path& assets, const Uuid::Uuid16& modelId,
                                            std::span<const experiment::Material> materials, std::string& error)
{
    for (const auto& material : materials)
    {
        const auto graphId = ModelMaterialGraphId(material.assetId.value);
        if (graphId.IsNil())
        {
            error = "Model material graph requires its UUIDv8 material identity";
            return false;
        }
        const auto path = ModelMaterialGraphPath(assets, modelId, material.assetId.value);
        const auto meta = std::filesystem::path(path.string() + ".meta");
        const bool hasGraph = std::filesystem::exists(path);
        const bool hasMeta = std::filesystem::exists(meta);
        if (hasGraph || hasMeta)
        {
            const auto document = Authoring::ParsedDocument::ParseText(ReadText(meta), error);
            if (!hasGraph || !hasMeta || !document || document.Root()["guid"].AsString() != Uuid::ToString(graphId) ||
                !LXMaterialAsset::Load(path, CreateMaterialDefinitions(), &error))
            {
                error = "Existing model material graph or identity is invalid: " + path.string() + ". " + error;
                return false;
            }
            continue;
        }
        const auto graph = BuildModelMaterialGraph(material, error);
        if (!graph)
            return false;
        const auto text = LXMaterialArchive::Write(*graph);
        const auto restored = LXMaterialArchive::Read(text, graph->Definitions(), &error);
        if (!restored || !graph->Equals(*restored))
            return false;
        std::error_code filesystemError;
        std::filesystem::create_directories(path.parent_path(), filesystemError);
        if (filesystemError)
        {
            error = filesystemError.message();
            return false;
        }
        for (const auto& [destination, payload] :
             std::array{std::pair{path, text}, std::pair{meta, "guid: " + Uuid::ToString(graphId) + "\n"}})
        {
            static std::atomic<std::uint64_t> serial{};
            const auto staging =
                std::filesystem::path(destination.string() + ".stage-" + std::to_string(GetCurrentProcessId()) + "-" +
                                      std::to_string(++serial));
            files_.push_back({staging, destination});
            std::ofstream output(staging, std::ios::binary | std::ios::trunc);
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            output.close();
            if (!output)
            {
                error = "Cannot stage model material graph: " + staging.string();
                return false;
            }
        }
    }
    return true;
}

bool ModelMaterialGraphPublication::Publish(std::string& error)
{
    for (auto& file : files_)
    {
        if (!MoveFileExW(file.staging.c_str(), file.destination.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            error = "Cannot publish model material graph (Win32 " + std::to_string(GetLastError()) +
                    "): " + file.destination.string();
            return false;
        }
        file.published = true;
    }
    return true;
}

void ModelMaterialGraphPublication::Commit()
{
    committed_ = true;
}
} // namespace assets
