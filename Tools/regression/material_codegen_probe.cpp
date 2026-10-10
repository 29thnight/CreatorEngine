#include "../../Lattice/Material/LXMaterialCompiler.h"
#include "material_probe_compiler.h"
#include "material_probe_gpu.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
using namespace LX;
using MaterialProbe::Float4;
std::size_t checks = 0, compiled = 0, rejected = 0, gpuChecks = 0;
constexpr unsigned FieldCount = 9;

void Check(bool condition, const std::string& message)
{
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

void Write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
    Check(out.good(), "Write " + path.string());
}

std::string Read(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    Check(in.good(), "Read " + path.string());
    return {std::istreambuf_iterator<char>(in), {}};
}

std::string ReverseRecords(std::string text, const std::string& key)
{
    const auto begin = text.find("\"" + key + "\":[") + key.size() + 4;
    Check(begin < text.size(), "Find archive array " + key);
    std::vector<std::string> records;
    std::size_t start = begin, end = begin;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (; end < text.size(); ++end)
    {
        const char character = text[end];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (character == '\\')
                escaped = true;
            else if (character == '"')
                quoted = false;
            continue;
        }
        if (character == '"')
            quoted = true;
        else if (character == '[' || character == '{')
            ++depth;
        else if (character == ']' && !depth)
            break;
        else if (character == ']' || character == '}')
            --depth;
        else if (character == ',' && !depth)
        {
            records.push_back(text.substr(start, end - start));
            start = end + 1;
        }
    }
    records.push_back(text.substr(start, end - start));
    std::ranges::reverse(records);
    std::string reversed;
    for (const auto& record : records)
        reversed += (reversed.empty() ? "" : ",") + record;
    text.replace(begin, end - begin, reversed);
    return text;
}

Id PinId(const LXGraph& graph, Id node, const std::string& identifier, Direction direction = Direction::Input)
{
    for (const auto& pin : graph.FindNode(node)->pins)
        if (pin.Identifier() == identifier && pin.direction == direction)
            return pin.id;
    throw std::runtime_error("Missing " + identifier);
}

void Connect(LXGraph& graph, Id from, const std::string& output, Id to, const std::string& input)
{
    Check(graph.Connect(PinId(graph, from, output, Direction::Output), PinId(graph, to, input)).has_value(),
          "Connect " + output + " to " + input);
}

void Set(LXGraph& graph, Id node, const std::string& name, LXSocketValue value, Direction side = Direction::Input)
{
    Check(graph.SetSocketValue(PinId(graph, node, name, side), std::move(value)), "Set " + name);
}

struct Input
{
    Float4 uv;
    Float4 config;
    Float4 tangent;
    Float4 bitangent;
};

using Fields = std::array<Float4, FieldCount>;
Fields Defaults()
{
    return {{{.8f, .8f, .8f, 1},
             {.5f, 0, 1.5f, .5f},
             {0, 0, 1, 0},
             {1, 1, 1, 0},
             {0, 0, 0, 0},
             {0, .05f, 1, .03f},
             {},
             {},
             {}}};
}

struct Case
{
    std::string name;
    LXMaterialAsset asset;
    Id surface = 0;
    Id image = 0;
    std::function<void(Fields&, const Input&)> expected;
    std::string overrides;
    bool golden = false;
};

Case Surface(const LXMaterialDefinitions& definitions, std::string name)
{
    Case result{std::move(name), LXMaterialAsset(definitions)};
    result.surface = result.asset.CreateNode("ShaderNodeBsdfPrincipled", 200, 0);
    result.asset.activeOutput = result.asset.CreateNode("ShaderNodeOutputMaterial", 600, 0);
    Check(result.surface && result.asset.activeOutput, "Create surface");
    Connect(result.asset.graph, result.surface, "BSDF", result.asset.activeOutput, "Surface");
    return result;
}

float Srgb(float value)
{
    return value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
}

const MaterialProbe::TextureFixture Texture{
    2, 2, {{.2f, .4f, .6f, .3f}, {.8f, .1f, .3f, .9f}, {.5f, .9f, .05f, 0}, {.02f, .3f, 1.5f, .7f}}};

Float4 Sample(Float4 uv, bool linear, bool repeat, bool srgb)
{
    const float x = uv.x * 2 - .5f, y = uv.y * 2 - .5f;
    const auto at = [&](int px, int py) {
        px = repeat ? (px % 2 + 2) % 2 : std::clamp(px, 0, 1);
        py = repeat ? (py % 2 + 2) % 2 : std::clamp(py, 0, 1);
        auto pixel = Texture.pixels[py * 2 + px];
        if (srgb)
            pixel = {Srgb(pixel.x), Srgb(pixel.y), Srgb(pixel.z), pixel.w};
        return pixel;
    };
    if (!linear)
        return at(static_cast<int>(std::floor(x + .5f)), static_cast<int>(std::floor(y + .5f)));
    const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
    const float fx = x - ix, fy = y - iy;
    const auto a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
    const auto blend = [&](float av, float bv, float cv, float dv) {
        return (av + (bv - av) * fx) * (1 - fy) + (cv + (dv - cv) * fx) * fy;
    };
    return {blend(a.x, b.x, c.x, d.x), blend(a.y, b.y, c.y, d.y), blend(a.z, b.z, c.z, d.z), blend(a.w, b.w, c.w, d.w)};
}

Float4 Normal(float strength, Float4 color, const Input& input, bool world)
{
    Float4 result{0, 0, 1, 0};
    if (strength <= 0 || (!world && input.tangent.x == 0))
        return result;
    const float x = (color.x * 2 - 1) * strength;
    float y = (color.y * 2 - 1) * strength;
    const float z = 1 + ((color.z * 2 - 1) - 1) * (world ? strength : std::clamp(strength, 0.0f, 1.0f));
    if (!world && input.bitangent.y < 0)
        y = -y;
    const float length = std::sqrt(x * x + y * y + z * z);
    if (length <= 1e-10f)
        return result;
    return {x / length, y / length, z / length, 0};
}

std::vector<Case> Cases(const LXMaterialDefinitions& definitions)
{
    std::vector<Case> cases;
    auto constant = Surface(definitions, "constant");
    const auto rgb = constant.asset.CreateNode("ShaderNodeRGB", 0, 0);
    Set(constant.asset.graph, rgb, "Color", std::array<double, 4>{.04, .5, 4, .2}, Direction::Output);
    constant.asset.scopes[0].sockets[PinId(constant.asset.graph, rgb, "Color", Direction::Output)].colorSpace =
        LXColorSpace::SRGB;
    Connect(constant.asset.graph, rgb, "Color", constant.surface, "Base Color");
    Set(constant.asset.graph, constant.surface, "Alpha", 0.0);
    Set(constant.asset.graph, constant.surface, "Emission Strength", 7.0);
    Set(constant.asset.graph, constant.surface, "Emission Color", std::array<double, 4>{2, .25, .5, 1});
    constant.expected = [](auto& fields, const auto&) {
        fields[0] = {Srgb(.04f), Srgb(.5f), Srgb(4), 0};
        fields[2].w = 7;
        fields[3] = {2, .25f, .5f, 0};
    };
    constant.golden = true;
    cases.push_back(constant);
    auto principledGroup = Surface(definitions, "principled-group");
    Check(principledGroup.asset.CollapseToGroup({principledGroup.surface}, "Principled group") != 0,
          "Group full Principled sockets");
    cases.push_back(principledGroup);

    auto parameter = Surface(definitions, "runtime-parameters");
    parameter.asset.blackboard = {
        {1001, "roughness", "Roughness", PinType::Float, 0.0, LXColorSpace::Data},
        {1002, "color", "Color", PinType::Color, std::array<double, 4>{.5, .25, .75, .2}, LXColorSpace::SRGB},
        {2000, "unused", "Unused", PinType::Int, std::numeric_limits<std::int64_t>::min(), LXColorSpace::Data}};
    for (const auto& [type, id, pin] : std::vector<std::tuple<std::string, Id, std::string>>{
             {"Float", 1001, "Roughness"}, {"Color", 1002, "Base Color"}})
    {
        const auto node = parameter.asset.CreateNode("LXParameter" + type, 0, 0);
        parameter.asset.graph.SetProperty(node, "parameter", std::to_string(id));
        Connect(parameter.asset.graph, node, "Value", parameter.surface, pin);
        if (id == 1001)
            Connect(parameter.asset.graph, node, "Value", parameter.surface, "Coat Weight");
    }
    parameter.overrides = "parameters.lx_p1001 = input.config.x;";
    parameter.asset.blackboard[0].name = "Roughness\n\"표시\"\t\\\b";
    parameter.expected = [](auto& fields, const auto& input) {
        fields[0] = {Srgb(.5f), Srgb(.25f), Srgb(.75f), 1};
        fields[1].x = input.config.x;
        fields[3].w = input.config.x;
    };
    cases.push_back(parameter);

    auto nested = Surface(definitions, "nested-shared");
    LXGraph body("material", definitions.nodes);
    const auto multiply = body.CreateNode("LXMultiplyFloat", 0, 0);
    Set(body, multiply, "B", 0.5);
    const auto group = nested.asset.graph.CreateGroup(
        "Multiply group", body,
        {{0, "factor", "Factor", Direction::Input, PinType::Float, PinId(body, multiply, "A"), 1.0},
         {0, "result", "Result", Direction::Output, PinType::Float,
          PinId(body, multiply, "Result", Direction::Output)}});
    Check(group != 0, "Create developer-defined group interface");
    const auto first = nested.asset.graph.CreateGroupInstance(group, 0, 0);
    const auto second = nested.asset.graph.CreateGroupInstance(group, 0, 100);
    Set(nested.asset.graph, first, "factor", .4);
    Set(nested.asset.graph, second, "factor", .8);
    Connect(nested.asset.graph, first, "result", nested.surface, "Roughness");
    Connect(nested.asset.graph, second, "result", nested.surface, "Metallic");
    Check(nested.asset.CollapseToGroup({first, second}, "Nested shared group") != 0, "Nest shared group");
    nested.expected = [](auto& fields, const auto&) {
        fields[1].x = .2f;
        fields[1].y = .4f;
    };
    nested.golden = true;
    cases.push_back(nested);

    for (bool linear : {false, true})
        for (bool repeat : {false, true})
            for (bool srgb : {false, true})
            {
                auto image = Surface(definitions, std::string("image-") + (linear ? "linear" : "nearest") +
                                                      (repeat ? "-repeat" : "-clamp") + (srgb ? "-srgb" : "-data"));
                image.image = image.asset.CreateNode("ShaderNodeTexImage", 0, 0);
                image.asset.graph.SetProperty(image.image, "image", "asset://fixture/image");
                image.asset.graph.SetProperty(image.image, "colorSpace", srgb ? "srgb" : "data");
                image.asset.graph.SetProperty(image.image, "interpolation", linear ? "Linear" : "Closest");
                image.asset.graph.SetProperty(image.image, "extension", repeat ? "REPEAT" : "EXTEND");
                const auto multiplyColor = image.asset.CreateNode("LXMultiplyColor", 100, 0);
                Set(image.asset.graph, multiplyColor, "B", std::array<double, 4>{2, .5, .25, 1});
                Connect(image.asset.graph, image.image, "Color", multiplyColor, "A");
                Connect(image.asset.graph, multiplyColor, "Result", image.surface, "Base Color");
                Connect(image.asset.graph, image.image, "Alpha", image.surface, "Alpha");
                image.expected = [=](auto& fields, const auto& input) {
                    const auto value = Sample(input.uv, linear, repeat, srgb);
                    fields[0] = {2 * value.x, .5f * value.y, .25f * value.z, value.w};
                };
                image.golden = linear && repeat && srgb;
                cases.push_back(image);
            }

    auto imageGroup =
        *std::ranges::find_if(cases, [](const auto& item) { return item.name == "image-linear-repeat-srgb"; });
    imageGroup.name = "image-group-implicit-uv";
    imageGroup.golden = false;
    Check(imageGroup.asset.CollapseToGroup({imageGroup.image}, "Image default UV") != 0,
          "Group image preserves implicit UV");
    cases.push_back(imageGroup);

    auto resources = Surface(definitions, "resource-parameters");
    resources.asset.blackboard = {
        {1001, "image", "Image", PinType::Texture, std::string("asset://fixture/image"), LXColorSpace::SRGB},
        {1002, "sampler", "Sampler", PinType::Sampler, std::string("nearest-clamp"), LXColorSpace::Data},
        {1003, "uv", "UV", PinType::Vector, std::array<double, 3>{.25, .75, 0}, LXColorSpace::Data}};
    const auto textureSample = resources.asset.CreateNode("LXTextureSample", 0, 0);
    for (const auto& [type, id, pin] : std::vector<std::tuple<std::string, Id, std::string>>{
             {"Texture", 1001, "Texture"}, {"Sampler", 1002, "Sampler"}, {"Vector", 1003, "Vector"}})
    {
        const auto node = resources.asset.CreateNode("LXParameter" + type, 0, 0);
        resources.asset.graph.SetProperty(node, "parameter", std::to_string(id));
        Connect(resources.asset.graph, node, "Value", textureSample, pin);
    }
    Connect(resources.asset.graph, textureSample, "Color", resources.surface, "Base Color");
    Connect(resources.asset.graph, textureSample, "Alpha", resources.surface, "Alpha");
    resources.expected = [](auto& fields, const auto&) { fields[0] = {Srgb(.5f), Srgb(.9f), Srgb(.05f), 0}; };
    cases.push_back(resources);

    for (bool world : {false, true})
    {
        auto normal = Surface(definitions, world ? "normal-world" : "normal-tangent");
        const auto node = normal.asset.CreateNode("ShaderNodeNormalMap", 0, 0);
        normal.asset.graph.SetProperty(node, "space", world ? "WORLD" : "TANGENT");
        Set(normal.asset.graph, node, "Color", std::array<double, 4>{.8, .2, .8, 1});
        Set(normal.asset.graph, node, "Strength", 1.5);
        Connect(normal.asset.graph, node, "Normal", normal.surface, "Normal");
        normal.expected = [=](auto& fields, const auto& input) {
            fields[2] = Normal(1.5f, {.8f, .2f, .8f, 1}, input, world);
        };
        cases.push_back(normal);
    }

    // Exercise the storage decoder at the post-sample boundary with the same
    // floating-point XY fixture. Runtime BC5 format selection is covered by the
    // product probe; this isolates normal reconstruction from BC compression.
    for (const bool bc5 : {false, true})
    {
        auto normalImage = Surface(definitions, bc5 ? "normal-image-bc5-encoding" : "normal-image-rgba-encoding");
        normalImage.image = normalImage.asset.CreateNode("ShaderNodeTexImage", 0, 0);
        auto& graph = normalImage.asset.graph;
        Check(graph.SetProperty(normalImage.image, "image", "asset://fixture/image"), "Normal image reference");
        Check(graph.SetProperty(normalImage.image, "colorSpace", "data"), "Normal image data encoding");
        Check(graph.SetProperty(normalImage.image, "interpolation", "Closest"), "Normal image alternate filter");
        Check(graph.SetProperty(normalImage.image, "interpolation", "Linear"), "Normal image filter");
        Check(graph.SetProperty(normalImage.image, "extension", "EXTEND"), "Normal image alternate address");
        Check(graph.SetProperty(normalImage.image, "extension", "REPEAT"), "Normal image address");
        const auto normalNode = normalImage.asset.CreateNode("ShaderNodeNormalMap", 100, 0);
        Set(graph, normalNode, "Strength", 0.5);
        Set(graph, normalNode, "Strength", 1.0);
        Connect(graph, normalImage.image, "Color", normalNode, "Color");
        Connect(graph, normalNode, "Normal", normalImage.surface, "Normal");
        Connect(graph, normalImage.image, "Color", normalImage.surface, "Base Color");
        Connect(graph, normalImage.image, "Alpha", normalImage.surface, "Alpha");
        normalImage.overrides = std::string("    parameters.lx_texture_0_encoding = ") + (bc5 ? "1u;" : "0u;");
        normalImage.expected = [bc5](auto& fields, const auto& input) {
            auto value = Sample(input.uv, true, true, false);
            if (bc5)
            {
                const float x = value.x * 2.0f - 1.0f;
                const float y = value.y * 2.0f - 1.0f;
                value.z = std::sqrt(std::clamp(1.0f - x * x - y * y, 0.0f, 1.0f)) * 0.5f + 0.5f;
            }
            fields[0] = value;
            fields[2] = Normal(1.0f, value, input, false);
        };
        cases.push_back(std::move(normalImage));
    }

    auto dead = Surface(definitions, "dead-lobes");
    dead.image = dead.asset.CreateNode("ShaderNodeTexImage", 0, 0);
    dead.asset.graph.SetProperty(dead.image, "interpolation", "Cubic");
    for (const auto& pin : {"Emission Color", "Coat Tint", "Sheen Tint"})
        Connect(dead.asset.graph, dead.image, "Color", dead.surface, pin);
    const auto normal = dead.asset.CreateNode("ShaderNodeNormalMap", 0, 0);
    Set(dead.asset.graph, normal, "Strength", 0.0);
    Connect(dead.asset.graph, dead.image, "Color", normal, "Color");
    Connect(dead.asset.graph, normal, "Normal", dead.surface, "Normal");
    Set(dead.asset.graph, dead.surface, "Metallic", 1.0);
    Set(dead.asset.graph, dead.surface, "Transmission Weight", .8);
    Set(dead.asset.graph, dead.surface, "Subsurface Weight", .9);
    Connect(dead.asset.graph, normal, "Normal", dead.surface, "Subsurface Radius");
    dead.expected = [](auto& fields, const auto&) { fields[1].y = 1; };
    cases.push_back(dead);
    auto glass = dead;
    glass.name = "full-glass";
    Set(glass.asset.graph, glass.surface, "Metallic", 0.0);
    Set(glass.asset.graph, glass.surface, "Transmission Weight", 1.0);
    glass.expected = [](auto& fields, const auto&) { fields[4].w = 1; };
    cases.push_back(glass);

    auto special = Surface(definitions, "special-volume");
    for (const auto& [pin, value] : std::vector<std::pair<std::string, double>>{{"Coat Weight", .3},
                                                                                {"Sheen Weight", .4},
                                                                                {"Anisotropic", .5},
                                                                                {"Thin Film Thickness", 320},
                                                                                {"Transmission Weight", .6},
                                                                                {"Subsurface Weight", .7}})
        Set(special.asset.graph, special.surface, pin, value);
    const auto volume = special.asset.CreateNode("LXPrincipledVolume", 0, 0);
    Set(special.asset.graph, volume, "Density", 2.0);
    Set(special.asset.graph, volume, "Absorption Color", std::array<double, 4>{.25, 0, 1, 1});
    Set(special.asset.graph, volume, "Emission Strength", 3.0);
    Set(special.asset.graph, volume, "Emission Color", std::array<double, 4>{2, 0, .5, 1});
    Set(special.asset.graph, volume, "Anisotropy", -.4);
    const auto reroute = special.asset.CreateNode("LXRerouteClosure", 0, 0);
    Connect(special.asset.graph, volume, "Volume", reroute, "Input");
    Connect(special.asset.graph, reroute, "Output", special.asset.activeOutput, "Volume");
    special.expected = [](auto& fields, const auto&) {
        fields[3].w = .3f;
        fields[4] = {.4f, .5f, 320, .6f};
        fields[5].x = .7f;
        fields[6] = {1, 1, 1, 0};
        fields[7] = {.5f, 1, 0, 0};
        fields[8] = {6, 0, 1.5f, -.4f};
    };
    special.golden = true;
    cases.push_back(special);
    auto volumeOnly = special;
    volumeOnly.name = "volume-only";
    Check(volumeOnly.asset.graph.RemoveNode(volumeOnly.surface), "Remove disconnected surface");
    volumeOnly.expected = [](auto& fields, const auto&) {
        fields[0].w = 0;
        fields[6] = {1, 1, 1, 0};
        fields[7] = {.5f, 1, 0, 0};
        fields[8] = {6, 0, 1.5f, -.4f};
    };
    cases.push_back(volumeOnly);
    return cases;
}

std::string Wrapper(const LXMaterialProgram& program, const std::string& overrides = {})
{
    std::string text = program.slang;
    text += R"(
struct ProbeInput { float4 uv; float4 config; float4 tangent; float4 bitangent; };
StructuredBuffer<ProbeInput> probeInputs : register(t0);
RWStructuredBuffer<float4> probeOutputs : register(u0);
void Probe(ProbeInput input, out float4 fields[9])
{
    LXMaterialContext context;
    context.uv = input.uv.xyz;
    context.lod = 0.0;
    context.normal = float3(0, 0, 1);
    context.tangent = input.tangent.xyz;
    context.bitangent = input.bitangent.xyz;
    LXMaterialParameters parameters = LXDefaultMaterialParameters();
)";
    text += overrides + "\n";
    text += R"(
    LXGeneratedMaterial material = LXGenerateMaterial(context, parameters);
    PrincipledSurface surface = LXEvaluateGeneratedSurface(material, context);
    MaterialInputs inputs = material.surfaceInputs;
    fields[0] = float4(inputs.baseColor, inputs.alpha);
    fields[1] = float4(inputs.roughness, inputs.metallic, inputs.ior, inputs.specularIorLevel);
    fields[2] = float4(surface.normal, inputs.emissionStrength);
    fields[3] = float4(inputs.emissionColor, inputs.coatWeight);
    fields[4] = float4(inputs.sheenWeight, inputs.anisotropy, inputs.thinFilmThickness, inputs.transmissionWeight);
    fields[5] = float4(inputs.subsurfaceWeight, inputs.subsurfaceScale, inputs.subsurfaceRadius.x, inputs.coatRoughness);
)";
    if (program.volume)
        text += "    PrincipledVolume volume = LXEvaluateGeneratedVolume(material);\n"
                "    fields[6] = float4(volume.scattering, 0);\n    fields[7] = float4(volume.absorption, 0);\n"
                "    fields[8] = float4(volume.emission, volume.anisotropy);\n";
    else
        text += "    fields[6] = fields[7] = fields[8] = float4(0);\n";
    text += R"(
}
[numthreads(1, 1, 1)]
void GeneratedCS(uint3 id : SV_DispatchThreadID)
{
    float4 fields[9];
    Probe(probeInputs[id.x], fields);
    for (uint index = 0; index < 9; ++index) probeOutputs[id.x * 9 + index] = fields[index];
}
float4 GeneratedPS(float4 position : SV_Position) : SV_Target0
{
    ProbeInput input;
    input.uv = float4(position.xy * 0.001, 0, 0);
    input.config = float4(0.7, 0, 0, 0);
    input.tangent = float4(1, 0, 0, 0);
    input.bitangent = float4(0, 1, 0, 0);
    float4 fields[9];
    Probe(input, fields);
    float4 result = float4(0);
    for (uint index = 0; index < 9; ++index) result += fields[index];
    return result;
}
)";
    return text;
}

LXMaterialProgram Generate(const LXMaterialAsset& asset)
{
    std::vector<LXMaterialDiagnostic> diagnostics;
    auto result = GenerateMaterialSlang(asset, &diagnostics);
    std::string error;
    for (const auto& diagnostic : diagnostics)
        error += diagnostic.code + ": " + diagnostic.message + "\n";
    Check(result.has_value(), "Generate material: " + error);
    Check(diagnostics.empty(), "Successful generation has no errors");
    return *result;
}

void ExpectError(const LXMaterialAsset& asset, const std::string& code, Id node = 0, Id pin = 0,
                 LXMaterialCompilerOptions options = {})
{
    std::vector<LXMaterialDiagnostic> diagnostics;
    Check(!GenerateMaterialSlang(asset, &diagnostics, options), "Reject " + code);
    Check(!diagnostics.empty() && diagnostics.front().code == code, "Diagnostic code " + code);
    if (node)
        Check(diagnostics.front().source.node == node, "Diagnostic node " + code);
    if (pin)
        Check(diagnostics.front().source.pin == pin, "Diagnostic socket " + code);
    ++rejected;
}

void NativeTests(const LXMaterialDefinitions& definitions, const std::vector<Case>& cases)
{
    const auto original = Generate(cases[0].asset);
    auto layout = cases[0].asset;
    layout.graph.SetNodePosition(cases[0].surface, -900, 420);
    layout.graph.SetNodeCollapsed(cases[0].surface, true);
    layout.graph.AddFrame("not shader source", -1000, 0, 100, 100, {cases[0].surface});
    layout.graph.SetView({true, 1, 2, .5});
    auto changed = Generate(layout);
    Check(original.slang == changed.slang && original.semanticKey == changed.semanticKey,
          "Layout excluded from code/cache key");
    auto dead = cases[0].asset;
    const auto unused = dead.CreateNode("ShaderNodeTexImage", 0, 0);
    dead.graph.SetProperty(unused, "interpolation", "Cubic");
    Check(Generate(dead).semanticKey == original.semanticKey, "Valid disconnected operators/resources eliminated");
    for (const auto& item : cases)
    {
        const auto program = Generate(item.asset);
        Check(Generate(item.asset).slang == program.slang, item.name + " repeated deterministic source");
        const auto restored = LXMaterialArchive::Read(LXMaterialArchive::Write(item.asset), definitions);
        Check(restored && Generate(*restored).semanticKey == program.semanticKey,
              item.name + " exact asset round-trip");
        const auto reorderedArchive = LXMaterialArchive::Read(
            ReverseRecords(ReverseRecords(LXMaterialArchive::Write(item.asset), "nodes"), "links"), definitions);
        Check(reorderedArchive && Generate(*reorderedArchive).semanticKey == program.semanticKey,
              item.name + " node/link storage order");
        auto reordered = item.asset;
        std::ranges::reverse(reordered.blackboard);
        Check(Generate(reordered).semanticKey == program.semanticKey, item.name + " parameter storage order");
        for (const auto& range : program.sourceMap)
            Check(FindMaterialSource(program, range.firstLine) == range.source, "Source map range");
        if (item.name.starts_with("image-"))
            Check(program.textureSamples == 1 && program.resources.size() == 2, "Color/Alpha share sample");
        if (item.name == "runtime-parameters")
            Check(program.parameters.size() == 2 && (program.features & 0x80),
                  "Unused parameters eliminated, dynamic zero-default lobe retained");
        if (item.name == "dead-lobes")
            Check(program.features == 0x4F && program.resources.empty(), "Folded lobes/resources eliminated");
        if (item.name == "full-glass")
            Check(program.features == 0x84F && !program.execution.subsurface, "Full transmission suppresses SSS");
        if (item.name == "special-volume")
            Check(program.features == 0x3FCF && program.execution.forward && program.execution.volume,
                  "All Layered/Special features extracted");
        if (item.name == "volume-only")
            Check(!program.surface && program.volume && program.features == 0x2000, "Separate Volume-only output");
    }
    auto invalid = Surface(definitions, "invalid");
    for (const char* pin : {"Diffuse Roughness", "Thickness", "Weight"})
    {
        auto asset = invalid.asset;
        const Id owner = std::string(pin) == "Thickness" ? asset.activeOutput : invalid.surface;
        Set(asset.graph, owner, pin, 0.1);
        ExpectError(asset, "material_unsupported_socket", owner, PinId(asset.graph, owner, pin));
    }
    auto displacement = invalid.asset;
    Set(displacement.graph, displacement.activeOutput, "Displacement", std::array<double, 3>{0, 1, 0});
    ExpectError(displacement, "material_unsupported_socket", displacement.activeOutput);
    auto target = invalid.asset;
    target.graph.SetProperty(target.activeOutput, "target", "EEVEE");
    ExpectError(target, "material_output_target", target.activeOutput);
    auto empty = invalid.asset;
    empty.graph.RemoveNode(invalid.surface);
    ExpectError(empty, "material_empty_output", empty.activeOutput);
    auto role = invalid.asset;
    role.graph.Disconnect(role.graph.Links().front().id);
    Connect(role.graph, invalid.surface, "BSDF", role.activeOutput, "Volume");
    ExpectError(role, "material_closure_role", role.activeOutput, PinId(role.graph, role.activeOutput, "Volume"));
    auto enormous = invalid.asset;
    Set(enormous.graph, invalid.surface, "Roughness", std::numeric_limits<double>::max());
    ExpectError(enormous, "material_value_range", invalid.surface, PinId(enormous.graph, invalid.surface, "Roughness"));
    ExpectError(invalid.asset, "material_complexity", 0, 0, {0});

    auto texture =
        std::ranges::find_if(cases, [](const auto& item) { return item.image && item.name.starts_with("image-"); });
    for (const auto& [property, value, code] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"image", "", "material_resource"},
             {"interpolation", "Cubic", "material_texture_interpolation"},
             {"projection", "BOX", "material_texture_projection"},
             {"extension", "CLIP", "material_texture_extension"},
             {"colorSpace", "bogus", "material_property"}})
    {
        auto asset = texture->asset;
        asset.graph.SetProperty(texture->image, property, value);
        ExpectError(asset, code, texture->image);
    }
    ExpectError(texture->asset, "material_resource_limit", texture->image, 0, {4096, 0});
    const auto parameterCase =
        std::ranges::find_if(cases, [](const auto& item) { return item.name == "runtime-parameters"; });
    ExpectError(parameterCase->asset, "material_parameter_limit", 0, 0, {4096, 64, 0});
    auto normalCase = std::ranges::find_if(cases, [](const auto& item) { return item.name == "normal-tangent"; });
    const auto normalId = normalCase->asset.graph.Nodes().back().id;
    for (const auto& [property, value, code] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {"space", "OBJECT", "material_normal_space"}, {"uvMap", "UV2", "material_normal_uv"}})
    {
        auto asset = normalCase->asset;
        asset.graph.SetProperty(normalId, property, value);
        ExpectError(asset, code, normalId);
    }
    auto nested = texture->asset;
    const auto instance = nested.CollapseToGroup({texture->image}, "Bad nested projection");
    const auto groupId = nested.graph.FindNode(instance)->groupId;
    auto body = *nested.graph.FindGroup(groupId)->body;
    const auto imageId = std::ranges::find(body.Nodes(), std::string("ShaderNodeTexImage"), &Node::type)->id;
    body.SetProperty(imageId, "projection", "BOX");
    Check(nested.graph.ReplaceGroupBody(groupId, body), "Replace nested body");
    std::vector<LXMaterialDiagnostic> diagnostics;
    Check(!GenerateMaterialSlang(nested, &diagnostics), "Reject nested implementation option");
    Check(diagnostics.front().source.scope == groupId && diagnostics.front().source.node == imageId &&
              diagnostics.front().source.instances == std::vector<Id>{instance} &&
              diagnostics.front().source.property == "projection",
          "Nested diagnostic has exact definition and invocation path");
    ++rejected;
    body.SetProperty(imageId, "projection", "INVALID");
    Check(nested.graph.ReplaceGroupBody(groupId, body), "Replace nested enum");
    Check(!GenerateMaterialSlang(nested, &diagnostics) && diagnostics.front().source.scope == groupId,
          "IR enum diagnosis preserves scope");
    ++rejected;
    auto keyChange = parameterCase->asset;
    keyChange.blackboard[0].name = "Display only";
    Check(Generate(keyChange).semanticKey == Generate(parameterCase->asset).semanticKey,
          "Display names excluded from key");
    keyChange.blackboard[0].value = .25;
    Check(Generate(keyChange).semanticKey != Generate(parameterCase->asset).semanticKey,
          "Live default participates in key");
}

void CheckSpirvBindings(const std::vector<std::uint8_t>& bytes, const LXMaterialProgram& program)
{
    Check(bytes.size() >= 20 && bytes.size() % 4 == 0, "SPIR-V extent");
    std::vector<std::uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    Check(words[0] == 0x07230203, "SPIR-V magic");
    std::map<std::uint32_t, std::uint32_t> bindings, sets;
    for (std::size_t index = 5; index < words.size();)
    {
        const unsigned count = words[index] >> 16, opcode = words[index] & 0xFFFF;
        Check(count != 0 && index + count <= words.size(), "SPIR-V instruction extent");
        if (opcode == 71 && count >= 4)
        {
            if (words[index + 2] == 33)
                bindings[words[index + 1]] = words[index + 3];
            if (words[index + 2] == 34)
                sets[words[index + 1]] = words[index + 3];
        }
        index += count;
    }
    std::set<std::uint32_t> materialBindings;
    for (const auto& [id, space] : sets)
        if (space == 1)
            Check(materialBindings.insert(bindings.at(id)).second, "SPIR-V descriptor set has no collision");
    Check(materialBindings.size() == program.resources.size(), "Compiled material descriptor count");
    for (const auto& resource : program.resources)
        Check(materialBindings.contains(resource.slot + (resource.kind == LXMaterialResourceKind::Texture
                                                             ? VulkanBindingModel::kShaderResourceShift
                                                             : VulkanBindingModel::kSamplerShift)),
              "Compiled SPIR-V descriptor matches logical resource table");
}

std::vector<LXMaterialShaderArtifact> Compile(MaterialProbe::CompilerRuntime& compiler,
                                              const std::filesystem::path& source,
                                              const std::filesystem::path& includes, const LXMaterialProgram& program)
{
    std::vector<LXMaterialShaderArtifact> artifacts;
    for (bool spirv : {false, true})
        for (bool pixel : {false, true})
        {
            LXMaterialShaderArtifact artifact{spirv ? "spirv" : "dxil", pixel ? "GeneratedPS" : "GeneratedCS"};
            std::string error;
            const bool success = compiler.Compile(
                source, artifact.entryPoint.c_str(), pixel ? SLANG_STAGE_FRAGMENT : SLANG_STAGE_COMPUTE, spirv,
                program.execution.forward ? std::vector<std::string>{"LX_MATERIAL_ROUTE=2"}
                                          : std::vector<std::string>{},
                error, &artifact.bytecode, {includes});
            Check(success, source.stem().string() + ": " + error);
            Check(!artifact.bytecode.empty(), "Nonempty compiled artifact");
            if (spirv)
                CheckSpirvBindings(artifact.bytecode, program);
            artifacts.push_back(std::move(artifact));
            ++compiled;
        }
    return artifacts;
}

void PublicationTests(MaterialProbe::CompilerRuntime& compiler, const std::filesystem::path& output,
                      const std::filesystem::path& includes, const Case& item)
{
    LXMaterialProgramStore store;
    std::vector<LXMaterialDiagnostic> diagnostics;
    std::size_t calls = 0;
    const auto verifier = [&](const auto& program, auto& artifacts, auto&) {
        ++calls;
        const auto source = output / "published.slang";
        Write(source, Wrapper(program));
        artifacts = Compile(compiler, source, includes, program);
        return true;
    };
    Check(store.Publish(item.asset, verifier, &diagnostics) && store.Generation() == 1,
          "Publish all four backend artifacts");
    const auto good = store.LastValidProgram()->semanticKey;
    const auto bytes = store.Artifacts()[0].bytecode;
    Check(store.Publish(item.asset, verifier) && calls == 1 && store.Generation() == 1,
          "Reuse unchanged semantic generation");
    auto asset = item.asset;
    Set(asset.graph, item.surface, "Roughness", .375);
    const auto fails = [&](const LXMaterialVerifier& callback, const std::string& name) {
        Check(!store.Publish(asset, callback, &diagnostics), "Reject " + name);
        Check(!diagnostics.empty() && store.Generation() == 1 && store.LastValidProgram()->semanticKey == good &&
                  store.Artifacts()[0].bytecode == bytes && store.Artifacts().size() == 4,
              "Retain last compiled generation: " + name);
        ++rejected;
    };
    fails({}, "missing verifier");
    fails([](const auto&, auto&, auto&) { return true; }, "empty artifact set");
    fails(
        [](const auto&, auto& artifacts, auto&) {
            artifacts = {{"dxil", "GeneratedCS", {1}}, {"dxil", "GeneratedCS", {2}}};
            return true;
        },
        "duplicate target");
    fails([](const auto&, auto&, auto&) -> bool { throw std::runtime_error("compiler exception"); }, "exception");
    bool sawCompileFailure = false, sawMappedDiagnostic = false;
    fails(
        [&](const auto& program, auto& artifacts, auto& errors) {
            const auto path = output / "backend-failure.slang";
            Write(path, Wrapper(program));
            std::string error;
            LXMaterialShaderArtifact artifact{"dxil", "GeneratedCS"};
            Check(compiler.Compile(path, "GeneratedCS", SLANG_STAGE_COMPUTE, false, {}, error, &artifact.bytecode,
                                   {includes}),
                  "First backend compiles");
            ++compiled;
            artifacts.push_back(artifact);
            const auto mapped = std::ranges::find_if(program.sourceMap, [&](const auto& range) {
                return range.source.pin == PinId(asset.graph, item.surface, "Roughness");
            });
            Check(mapped != program.sourceMap.end(), "Roughness generated line map");
            auto broken = Wrapper(program);
            std::size_t begin = 0;
            for (unsigned line = 1; line < mapped->firstLine; ++line)
                begin = broken.find('\n', begin) + 1;
            const auto end = broken.find('\n', begin);
            broken.replace(begin, end - begin, "    this_is_a_deliberate_compile_failure;");
            Write(path, broken);
            sawCompileFailure =
                !compiler.Compile(path, "GeneratedCS", SLANG_STAGE_COMPUTE, true, {}, error, nullptr, {includes});
            Write(output / "backend-failure.log", error);
            errors = MapMaterialCompilerDiagnostics(program, path, error);
            sawMappedDiagnostic = std::ranges::any_of(errors, [&](const auto& diagnostic) {
                return diagnostic.source.node == item.surface &&
                       diagnostic.source.pin == PinId(asset.graph, item.surface, "Roughness");
            });
            return false;
        },
        "partial backend compilation");
    Check(sawCompileFailure && sawMappedDiagnostic,
          "Real Slang failure maps to node/socket outside publisher exception boundary");
    auto invalid = asset;
    Set(invalid.graph, item.surface, "Diffuse Roughness", .1);
    Check(!store.Publish(invalid, verifier) && calls == 1 && store.Generation() == 1,
          "Generator failure preserves artifacts without backend call");
    Check(store.Publish(asset, verifier) && store.Generation() == 2 && calls == 2,
          "Corrected edit publishes next generation");
    const auto& program = *store.LastValidProgram();
    const auto range = program.sourceMap.front();
    const auto fake = output / "published.slang";
    const auto mapped = MapMaterialCompilerDiagnostics(program, fake,
                                                       fake.string() + ":" + std::to_string(range.firstLine) +
                                                           ":3: warning 100: example\n"
                                                           "PrincipledSurface.slang(10): error 200: include error\n");
    Check(mapped.size() == 2 && mapped[0].source == range.source && mapped[0].severity == Issue::Severity::Warning &&
              !mapped[1].source.node,
          "Colon locations, warnings, and common include diagnostics");
    LXMaterialCompilerOptions options;
    options.dependencies = "common-shaders-sha256/changed;slang-2026.14;dxil+spirv;CS+PS";
    Check(store.Publish(asset, verifier, nullptr, options) && store.Generation() == 3 && calls == 3,
          "Dependency identity invalidates compiled generation");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error("Expected repository path");
        const std::filesystem::path root = argv[1];
        const auto output = root / "Build/Obj/MaterialCodegenProbe";
        const auto golden = root / "Tools/regression/fixtures/material-codegen";
        const auto includes = root / "Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes";
        const bool writeGolden = argc == 3 && std::string(argv[2]) == "--write-golden";
        std::filesystem::create_directories(output);
        if (writeGolden)
            std::filesystem::create_directories(golden);
        const auto definitions = CreateMaterialDefinitions();
        const auto cases = Cases(definitions);
        NativeTests(definitions, cases);
        MaterialProbe::CompilerRuntime compiler;
        compiler.Initialize(root);
        std::vector<Input> inputs{{{.25f, .25f, 0, 0}, {.25f, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}},
                                  {{.5f, .5f, 0, 0}, {.7f, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}},
                                  {{1.25f, -.25f, 0, 0}, {1.1f, 0, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}},
                                  {{.25f, .75f, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 1, 0, 0}},
                                  {{0, .125f, 0, 0}, {.9f, 0, 0, 0}, {1, 0, 0, 0}, {0, -1, 0, 0}}};
        std::ostringstream manifest, numeric;
        manifest << "case,features,samples,textures,samplers,parameters,sourceBytes\n";
        numeric << "case,input,field,x,y,z,w\n" << std::setprecision(9);
        for (const auto& item : cases)
        {
            const auto program = Generate(item.asset);
            const auto path = output / (item.name + ".slang");
            Write(path, Wrapper(program, item.overrides));
            const auto metadata = WriteMaterialProgramMetadata(program);
            Write(output / (item.name + ".materialprogram.json"), metadata);
            Check(metadata == WriteMaterialProgramMetadata(Generate(item.asset)), "Deterministic generated metadata");
            if (item.golden)
            {
                const auto expectedSource = golden / (item.name + ".slang");
                const auto expectedGraph = golden / (item.name + ".shadergraph");
                if (writeGolden)
                {
                    Write(expectedSource, program.slang);
                    Write(expectedGraph, LXMaterialArchive::Write(item.asset));
                }
                else
                {
                    Check(Read(expectedSource) == program.slang, "Exact pinned deterministic source " + item.name);
                    const auto restored = LXMaterialArchive::Read(Read(expectedGraph), definitions);
                    Check(restored && Generate(*restored).semanticKey == program.semanticKey,
                          "Pinned graph produces same program " + item.name);
                }
            }
            const auto artifacts = Compile(compiler, path, includes, program);
            std::vector<MaterialProbe::TextureFixture> textures;
            std::vector<MaterialProbe::SamplerFixture> samplers;
            for (const auto& resource : program.resources)
            {
                if (resource.kind == LXMaterialResourceKind::Texture)
                {
                    auto data = Texture;
                    if (resource.colorSpace == LXColorSpace::SRGB)
                        for (auto& pixel : data.pixels)
                            pixel = {Srgb(pixel.x), Srgb(pixel.y), Srgb(pixel.z), pixel.w};
                    textures.push_back(std::move(data));
                }
                else
                    samplers.push_back(
                        {resource.reference.starts_with("linear"), resource.reference.ends_with("repeat")});
            }
            MaterialProbe::ComputeReadback gpu;
            const auto results = gpu.Run(artifacts[0].bytecode, inputs, 1, FieldCount, textures, samplers);
            Check(results.size() == inputs.size() * FieldCount, "GPU readback extent");
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto fields = Defaults();
                if (item.expected)
                    item.expected(fields, inputs[index]);
                for (unsigned field = 0; field < FieldCount; ++field)
                {
                    const auto& actual = results[index * FieldCount + field];
                    const auto& expected = fields[field];
                    const std::array<float, 4> a{actual.x, actual.y, actual.z, actual.w},
                        b{expected.x, expected.y, expected.z, expected.w};
                    for (unsigned channel = 0; channel < 4; ++channel)
                    {
                        Check(std::isfinite(a[channel]) &&
                                  std::abs(a[channel] - b[channel]) <= 3e-5f * std::max(1.0f, std::abs(b[channel])),
                              item.name + " GPU input=" + std::to_string(index) + " field=" + std::to_string(field) +
                                  " channel=" + std::to_string(channel) + " actual=" + std::to_string(a[channel]) +
                                  " expected=" + std::to_string(b[channel]));
                        ++gpuChecks;
                    }
                    numeric << item.name << ',' << index << ',' << field << ',' << actual.x << ',' << actual.y << ','
                            << actual.z << ',' << actual.w << '\n';
                }
            }
            if (program.execution.forward)
                for (bool spirv : {false, true})
                {
                    std::string error;
                    Check(!compiler.Compile(path, "GeneratedCS", SLANG_STAGE_COMPUTE, spirv, {"LX_MATERIAL_ROUTE=1"},
                                            error, nullptr, {includes}) &&
                              error.find("Forward") != std::string::npos,
                          "Special rejects Deferred target");
                    ++rejected;
                }
            manifest << item.name << ',' << program.features << ',' << program.textureSamples << ',' << textures.size()
                     << ',' << samplers.size() << ',' << program.parameters.size() << ',' << program.slang.size()
                     << '\n';
            std::cout << "CASE_OK " << item.name << " features=" << program.features
                      << " samples=" << program.textureSamples << '\n';
        }
        PublicationTests(compiler, output, includes, cases[0]);
        Write(output / "manifest.csv", manifest.str());
        Write(output / "gpu-values.csv", numeric.str());
        std::cout << "LX_MATERIAL_CODEGEN_OK cases=" << cases.size() << " checks=" << checks << " compiled=" << compiled
                  << " rejected=" << rejected << " gpuChecks=" << gpuChecks << "\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "LX_MATERIAL_CODEGEN_FAILED check=" << checks << " " << error.what() << "\n";
        return 1;
    }
}
