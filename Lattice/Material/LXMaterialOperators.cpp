#include "LXMaterialOperators.h"

#include <stdexcept>

namespace LX
{
void RegisterMaterialOperators(LXMaterialDefinitions& result, LXNodeDefinitionRegistry& registry)
{
    const auto add = [&](NodeSpec spec) {
        LXMaterialNodeSchema schema;
        if (spec.type == "LXTextureSample")
        {
            schema.enumProperties = {{"colorSpace", {"data", "linear", "srgb"}}};
        }
        else if (spec.type == "LXSurfaceSettings")
        {
            schema.enumProperties = {{"alphaMode", {"opaque", "masked", "transparent"}}};
        }
        for (const auto& pin : spec.pins)
        {
            schema.sockets.push_back(
                {true, false, false, pin.type == PinType::Color ? LXColorSpace::Linear : LXColorSpace::Data});
        }
        const auto type = spec.type;
        std::string error;
        if (!registry.Register({"material", std::move(spec)}, &error))
        {
            throw std::logic_error(error);
        }
        result.schemas.emplace(type, std::move(schema));
    };
    for (PinType type : {PinType::Float, PinType::Color})
    {
        const LXSocketValue one =
            type == PinType::Float ? LXSocketValue(1.0) : LXSocketValue(std::array<double, 4>{1, 1, 1, 1});
        NodeSpec multiply;
        multiply.type = std::string("LXMultiply") + PinTypeName(type);
        multiply.title = "Multiply";
        multiply.pins = {{0, 0, "A", Direction::Input, type, false, false, "A", one},
                         {0, 0, "B", Direction::Input, type, false, false, "B", one},
                         {0, 0, "Result", Direction::Output, type, false, false, "Result"}};
        add(std::move(multiply));
    }
    NodeSpec sample;
    sample.type = "LXTextureSample";
    sample.title = "Texture Sample";
    sample.pins = {
        {0, 0, "Texture", Direction::Input, PinType::Texture, false, false, "Texture", std::string()},
        {0, 0, "Sampler", Direction::Input, PinType::Sampler, false, false, "Sampler", std::string("linear-repeat")},
        {0, 0, "Vector", Direction::Input, PinType::Vector, false, false, "Vector", std::array<double, 3>{0, 0, 0}},
        {0, 0, "Color", Direction::Output, PinType::Color, false, false, "Color"},
        {0, 0, "Alpha", Direction::Output, PinType::Float, false, false, "Alpha"}};
    sample.properties = {{"colorSpace", "data"}};
    add(std::move(sample));

    NodeSpec separate;
    separate.type = "ShaderNodeSeparateColor";
    separate.title = "Separate Color";
    separate.pins = {
        {0, 0, "Color", Direction::Input, PinType::Color, false, false, "Color", std::array<double, 4>{0, 0, 0, 1}},
        {0, 0, "Red", Direction::Output, PinType::Float, false, false, "Red"},
        {0, 0, "Green", Direction::Output, PinType::Float, false, false, "Green"},
        {0, 0, "Blue", Direction::Output, PinType::Float, false, false, "Blue"}};
    add(std::move(separate));

    NodeSpec coordinates;
    coordinates.type = "LXTextureCoordinates";
    coordinates.title = "Texture Coordinates";
    coordinates.pins = {
        {0, 0, "Offset", Direction::Input, PinType::Vector, false, false, "Offset", std::array<double, 3>{0, 0, 0}},
        {0, 0, "Scale", Direction::Input, PinType::Vector, false, false, "Scale", std::array<double, 3>{1, 1, 1}},
        {0, 0, "Rotation", Direction::Input, PinType::Float, false, false, "Rotation", 0.0},
        {0, 0, "Vector", Direction::Output, PinType::Vector, false, false, "Vector"}};
    add(std::move(coordinates));

    NodeSpec surface;
    surface.type = "LXSurfaceSettings";
    surface.title = "Surface Settings";
    surface.pins = {{0, 0, "Surface", Direction::Input, PinType::Closure, false, false, "Surface"},
                    {0, 0, "Occlusion", Direction::Input, PinType::Float, false, false, "Occlusion", 1.0},
                    {0, 0, "Strength", Direction::Input, PinType::Float, false, false, "Strength", 1.0},
                    {0, 0, "Alpha Cutoff", Direction::Input, PinType::Float, false, false, "Alpha Cutoff", 0.5},
                    {0, 0, "Surface", Direction::Output, PinType::Closure, false, false, "Surface"}};
    surface.properties = {{"alphaMode", "opaque"}};
    add(std::move(surface));

    NodeSpec volume;
    volume.type = "LXPrincipledVolume";
    volume.title = "Homogeneous Principled Volume";
    // MAT-5's six-input Blender-compatible homogeneous subset. This is not a
    // registration of Blender's full string-attribute/blackbody Volume node.
    volume.pins = {
        {0, 0, "Color", Direction::Input, PinType::Color, false, false, "Color", std::array<double, 4>{.5, .5, .5, 1}},
        {0, 0, "Density", Direction::Input, PinType::Float, false, false, "Density", 1.0},
        {0, 0, "Anisotropy", Direction::Input, PinType::Float, false, false, "Anisotropy", 0.0},
        {0, 0, "Absorption Color", Direction::Input, PinType::Color, false, false, "Absorption Color",
         std::array<double, 4>{0, 0, 0, 1}},
        {0, 0, "Emission Strength", Direction::Input, PinType::Float, false, false, "Emission Strength", 0.0},
        {0, 0, "Emission Color", Direction::Input, PinType::Color, false, false, "Emission Color",
         std::array<double, 4>{1, 1, 1, 1}},
        {0, 0, "Volume", Direction::Output, PinType::Closure, false, false, "Volume"}};
    add(std::move(volume));
    NodeSpec reroute;
    reroute.type = "LXRerouteClosure";
    reroute.title = "Reroute";
    reroute.pins = {{0, 0, "Input", Direction::Input, PinType::Closure, false, false, "Input"},
                    {0, 0, "Output", Direction::Output, PinType::Closure, false, false, "Output"}};
    add(std::move(reroute));
}
} // namespace LX
