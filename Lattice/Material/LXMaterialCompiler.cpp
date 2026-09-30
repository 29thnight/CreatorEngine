#include "LXMaterialCompiler.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace LX
{
namespace
{
enum class Closure
{
    None,
    Surface,
    Volume
};

struct Expression
{
    PinType type = PinType::Float;
    std::string text;
    std::optional<LXSocketValue> constant;
    Closure closure = Closure::None;
    LXColorSpace encoding = LXColorSpace::Data;
};

struct Statement
{
    std::string text;
    LXMaterialSource source;
};

struct Invocation
{
    const LXMaterialIRScope* scope{};
    Invocation* parent{};
    const LXMaterialIRNode* instance{};
    std::vector<Id> path;
    std::map<Id, Expression> values;
    std::map<Id, std::unique_ptr<Invocation>> children;
    std::set<Id> resolving;
};

class GenerationError : public std::runtime_error
{
  public:
    explicit GenerationError(LXMaterialDiagnostic diagnostic)
        : std::runtime_error(diagnostic.message), diagnostic(std::move(diagnostic))
    {
    }
    LXMaterialDiagnostic diagnostic;
};

std::string FloatLiteral(double value)
{
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
    {
        throw std::runtime_error("Value cannot be represented as a finite shader float");
    }
    float rounded = static_cast<float>(value);
    if (rounded == 0.0f)
        rounded = 0.0f;
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), rounded, std::chars_format::general,
                                      std::numeric_limits<float>::max_digits10);
    std::string text(buffer, result.ptr);
    if (text.find_first_of(".eE") == std::string::npos)
        text += ".0";
    return text;
}

std::string TypeName(PinType type)
{
    switch (type)
    {
    case PinType::Bool:
        return "bool";
    case PinType::Int:
        return "int";
    case PinType::Float:
        return "float";
    case PinType::Color:
        return "float4";
    case PinType::Vector:
    case PinType::Normal:
        return "float3";
    default:
        throw std::runtime_error("Socket does not have a numeric shader type");
    }
}

LXColorSpace Encoding(const std::string& value)
{
    if (value == "srgb")
        return LXColorSpace::SRGB;
    if (value == "linear")
        return LXColorSpace::Linear;
    if (value == "data")
        return LXColorSpace::Data;
    throw std::runtime_error("Unsupported texture color space");
}

double Srgb(double value)
{
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}

std::optional<double> Scalar(const Expression& value)
{
    if (value.constant && std::holds_alternative<double>(*value.constant))
        return std::get<double>(*value.constant);
    return std::nullopt;
}

bool Zero(const Expression& value)
{
    const auto scalar = Scalar(value);
    return scalar && *scalar == 0.0;
}

bool Active(const Expression& value)
{
    const auto scalar = Scalar(value);
    return !scalar || *scalar > 0.0;
}

const LXMaterialIRNode& NodeFor(const Invocation& invocation, Id pin)
{
    for (const auto& node : invocation.scope->nodes)
        for (const auto& socket : node.sockets)
            if (socket.id == pin)
                return node;
    throw std::runtime_error("IR socket owner is missing");
}

const LXMaterialIRSocket& Socket(const LXMaterialIRNode& node, const std::string& name, Direction direction)
{
    for (const auto& socket : node.sockets)
        if (socket.identifier == name && socket.direction == direction)
            return socket;
    throw std::runtime_error("IR socket definition is missing: " + name);
}

class Generator
{
  public:
    Generator(const LXMaterialIR& ir, LXMaterialCompilerOptions options) : ir_(ir), options_(options) {}

    LXMaterialProgram Run()
    {
        Invocation root;
        root.scope = Scope(0);
        const auto output = std::ranges::find(root.scope->nodes, ir_.activeOutput, &LXMaterialIRNode::id);
        if (output == root.scope->nodes.end())
            throw std::runtime_error("Active output is missing");
        Properties(root, *output, {"target"});
        if (output->properties.at("target") != "ALL")
            Fail(root, *output, nullptr, "material_output_target", "Only ALL output target is supported", "target");
        for (const char* name : {"Displacement", "Thickness"})
        {
            const auto& socket = Socket(*output, name, Direction::Input);
            auto value = Resolve(root, socket.id);
            bool neutral = Zero(value);
            if (value.constant && std::holds_alternative<std::array<double, 3>>(*value.constant))
                neutral = std::get<std::array<double, 3>>(*value.constant) == std::array<double, 3>{};
            if (!neutral)
                Fail(root, *output, &socket, "material_unsupported_socket", std::string(name) + " is not implemented");
        }
        const auto& surfacePin = Socket(*output, "Surface", Direction::Input);
        const auto& volumePin = Socket(*output, "Volume", Direction::Input);
        auto surface = Resolve(root, surfacePin.id), volume = Resolve(root, volumePin.id);
        if (surface.closure != Closure::None && surface.closure != Closure::Surface)
            Fail(root, *output, &surfacePin, "material_closure_role", "Volume closure cannot feed Surface output");
        if (volume.closure != Closure::None && volume.closure != Closure::Volume)
            Fail(root, *output, &volumePin, "material_closure_role", "Surface closure cannot feed Volume output");
        program_.surface = surface.closure == Closure::Surface;
        program_.volume = volume.closure == Closure::Volume;
        if (!program_.surface && !program_.volume)
            Fail(root, *output, &surfacePin, "material_empty_output", "Connect a Surface or Volume closure");
        if (program_.volume)
            program_.features |= 0x2000u;
        program_.execution = LXAnalyzeMaterialExecution(program_.features);
        Render(surface, volume);
        CanonicalKey();
        return std::move(program_);
    }

  private:
    const LXMaterialIR& ir_;
    LXMaterialCompilerOptions options_;
    LXMaterialProgram program_;
    std::vector<Statement> statements_;
    std::set<std::string> contextDependent_;
    std::map<Id, LXMaterialParameter> parameters_;
    std::map<std::string, std::size_t> resources_;
    std::size_t expressions_ = 0;
    std::uint32_t line_ = 1;

    const LXMaterialIRScope* Scope(Id id) const
    {
        for (const auto& scope : ir_.scopes)
            if (scope.groupId == id)
                return &scope;
        throw std::runtime_error("Referenced IR scope is missing");
    }

    LXMaterialSource Source(const Invocation& invocation, const LXMaterialIRNode& node,
                            const LXMaterialIRSocket* socket = nullptr, std::string property = {}) const
    {
        return {invocation.scope->groupId, node.id, socket ? socket->id : 0, invocation.path, std::move(property)};
    }

    [[noreturn]] void Fail(const Invocation& invocation, const LXMaterialIRNode& node, const LXMaterialIRSocket* socket,
                           const std::string& code, const std::string& message, const std::string& property = {}) const
    {
        throw GenerationError({code, message, Source(invocation, node, socket, property)});
    }

    void Properties(const Invocation& invocation, const LXMaterialIRNode& node,
                    std::initializer_list<std::string> allowed) const
    {
        for (const auto& [key, value] : node.properties)
        {
            (void)value;
            if (std::ranges::find(allowed, key) == allowed.end())
                Fail(invocation, node, nullptr, "material_unsupported_property", "Unsupported property: " + key, key);
        }
    }

    std::string Name(const Invocation& invocation, const LXMaterialIRNode& node) const
    {
        std::string result = "lx";
        for (Id instance : invocation.path)
            result += "_g" + std::to_string(instance);
        return result + "_n" + std::to_string(node.id);
    }

    void Emit(std::string text, LXMaterialSource source)
    {
        const auto assignment = text.find('=');
        if (assignment != std::string::npos)
        {
            const std::string expression = text.substr(assignment + 1);
            const std::regex identifiers("[A-Za-z_][A-Za-z_0-9]*");
            bool dependent = false;
            for (std::sregex_iterator token(expression.begin(), expression.end(), identifiers), end; token != end;
                 ++token)
            {
                dependent |= token->str() == "context" || contextDependent_.contains(token->str());
            }
            if (dependent)
            {
                const std::string target = text.substr(0, assignment);
                const std::regex variable("lx(?:_g[0-9]+)*_n[0-9]+(?:_sample)?");
                std::smatch match;
                if (std::regex_search(target, match, variable))
                {
                    contextDependent_.insert(match.str());
                }
            }
        }
        statements_.push_back({std::move(text), std::move(source)});
    }

    Expression Literal(LXSocketValue value, PinType type, LXColorSpace colorSpace, const LXMaterialSource& source) const
    {
        try
        {
            if (std::holds_alternative<std::monostate>(value))
            {
                if (type == PinType::Closure)
                    return {type};
                if (type == PinType::Color)
                    value = std::array<double, 4>{};
                else if (type == PinType::Vector || type == PinType::Normal)
                    value = std::array<double, 3>{};
                else if (type == PinType::Float)
                    value = 0.0;
                else if (type == PinType::Int)
                    value = std::int64_t{};
                else if (type == PinType::Bool)
                    value = false;
                else
                    value = std::string();
            }
            std::string text;
            if (type == PinType::Float)
            {
                text = FloatLiteral(std::get<double>(value));
                value = static_cast<double>(static_cast<float>(std::get<double>(value)));
            }
            else if (type == PinType::Bool)
                text = std::get<bool>(value) ? "true" : "false";
            else if (type == PinType::Int)
            {
                const auto integer = std::get<std::int64_t>(value);
                if (integer < std::numeric_limits<std::int32_t>::min() ||
                    integer > std::numeric_limits<std::int32_t>::max())
                    throw std::runtime_error("Live Int parameter exceeds shader int32 range");
                text =
                    integer == std::numeric_limits<std::int32_t>::min() ? "(-2147483647 - 1)" : std::to_string(integer);
            }
            else if (type == PinType::Vector || type == PinType::Normal)
            {
                auto vector = std::get<std::array<double, 3>>(value);
                text = "float3(" + FloatLiteral(vector[0]) + ", " + FloatLiteral(vector[1]) + ", " +
                       FloatLiteral(vector[2]) + ")";
                for (auto& component : vector)
                    component = static_cast<double>(static_cast<float>(component));
                value = vector;
            }
            else if (type == PinType::Color)
            {
                auto color = std::get<std::array<double, 4>>(value);
                if (colorSpace == LXColorSpace::SRGB)
                    for (std::size_t index = 0; index < 3; ++index)
                        color[index] = Srgb(color[index]);
                text = "float4(";
                for (std::size_t index = 0; index < 4; ++index)
                {
                    text += (index ? ", " : "") + FloatLiteral(color[index]);
                    color[index] = static_cast<double>(static_cast<float>(color[index]));
                }
                text += ")";
                value = color;
            }
            else
                text = std::get<std::string>(value);
            return {type, text, std::move(value), Closure::None, colorSpace};
        }
        catch (const std::exception& error)
        {
            throw GenerationError({"material_value_range", error.what(), source});
        }
    }

    const LXMaterialIRLink* Incoming(const Invocation& invocation, Id pin) const
    {
        for (const auto& link : invocation.scope->links)
            if (link.input == pin)
                return &link;
        return nullptr;
    }

    Expression Resolve(Invocation& invocation, Id pin)
    {
        if (invocation.values.contains(pin))
            return invocation.values.at(pin);
        const auto& node = NodeFor(invocation, pin);
        const auto found = std::ranges::find(node.sockets, pin, &LXMaterialIRSocket::id);
        const auto& socket = *found;
        if (++expressions_ > options_.maxExpressions || !invocation.resolving.insert(pin).second)
            Fail(invocation, node, &socket, "material_complexity", "Expression limit or recursive dependency");
        Expression value;
        const auto boundary = std::ranges::find_if(invocation.scope->groupInterface, [&](const auto& item) {
            return item.direction == Direction::Input && item.internalPin == pin;
        });
        if (invocation.parent && boundary != invocation.scope->groupInterface.end())
        {
            const auto external =
                std::ranges::find(invocation.instance->sockets, boundary->id, &LXMaterialIRSocket::interfaceId);
            value = Resolve(*invocation.parent, external->id);
        }
        else if (socket.direction == Direction::Input)
        {
            if (const auto* link = Incoming(invocation, pin))
                value = Resolve(invocation, link->output);
            else
                value = Literal(socket.value, socket.type, socket.state.colorSpace, Source(invocation, node, &socket));
        }
        else if (node.groupId)
        {
            auto& child = invocation.children[node.id];
            if (!child)
            {
                child = std::make_unique<Invocation>();
                child->scope = Scope(node.groupId);
                child->parent = &invocation;
                child->instance = &node;
                child->path = invocation.path;
                child->path.push_back(node.id);
            }
            const auto output = std::ranges::find(child->scope->groupInterface, socket.interfaceId, &LXGroupSocket::id);
            value = Resolve(*child, output->internalPin);
        }
        else
            value = Operator(invocation, node, socket);
        invocation.resolving.erase(pin);
        invocation.values[pin] = value;
        return value;
    }

    Expression Input(Invocation& invocation, const LXMaterialIRNode& node, const std::string& name)
    {
        return Resolve(invocation, Socket(node, name, Direction::Input).id);
    }

    Expression Resource(LXMaterialResourceKind kind, std::string reference, Id parameter, LXColorSpace encoding,
                        const LXMaterialSource& source)
    {
        if (reference.empty())
            throw GenerationError({"material_resource", "Live resource reference is empty", source});
        if (kind == LXMaterialResourceKind::Sampler &&
            !std::regex_match(reference, std::regex("(linear|nearest)-(repeat|clamp)|(linear|nearest)-(linear|nearest)-"
                                                    "(repeat|clamp|mirror)-(repeat|clamp|mirror)")))
            throw GenerationError({"material_sampler", "Unsupported sampler description: " + reference, source});
        const std::string key = std::to_string(int(kind)) + "/" + std::to_string(parameter) + "/" +
                                std::to_string(int(encoding)) + "/" + reference;
        if (!resources_.contains(key))
        {
            if (program_.resources.size() >= options_.maxResources)
                throw GenerationError({"material_resource_limit", "Material resource limit exceeded", source});
            const auto slot =
                static_cast<std::uint32_t>(std::ranges::count(program_.resources, kind, &LXMaterialResource::kind));
            resources_[key] = program_.resources.size();
            program_.resources.push_back({kind, slot, parameter, std::move(reference), encoding, source});
        }
        const auto& entry = program_.resources[resources_.at(key)];
        return {kind == LXMaterialResourceKind::Texture ? PinType::Texture : PinType::Sampler,
                (kind == LXMaterialResourceKind::Texture ? "lx_texture_" : "lx_sampler_") + std::to_string(entry.slot),
                std::nullopt, Closure::None, encoding};
    }

    Expression Parameter(Invocation& invocation, const LXMaterialIRNode& node, const LXMaterialIRSocket& socket)
    {
        Properties(invocation, node, {"parameter", "valueSource"});
        Id id{};
        const auto& text = node.properties.at("parameter");
        std::from_chars(text.data(), text.data() + text.size(), id);
        const auto parameter = std::ranges::find(ir_.blackboard, id, &LXMaterialParameter::id);
        if (parameters_.size() >= options_.maxParameters && !parameters_.contains(id))
            Fail(invocation, node, &socket, "material_parameter_limit", "Live parameter limit exceeded");
        Literal(parameter->value, parameter->type, LXColorSpace::Data, Source(invocation, node, &socket));
        parameters_[id] = *parameter;
        if (socket.type == PinType::Texture || socket.type == PinType::Sampler)
            return Resource(
                socket.type == PinType::Texture ? LXMaterialResourceKind::Texture : LXMaterialResourceKind::Sampler,
                std::get<std::string>(parameter->value), id, parameter->colorSpace, Source(invocation, node, &socket));
        auto value = Expression{socket.type, "parameters.lx_p" + std::to_string(id)};
        if (socket.type == PinType::Color && parameter->colorSpace == LXColorSpace::SRGB)
            value.text = "LXDecodeSrgb(" + value.text + ")";
        return value;
    }

    Expression Sample(Invocation& invocation, const LXMaterialIRNode& node, const LXMaterialIRSocket& socket)
    {
        const auto& colorPin = Socket(node, "Color", Direction::Output);
        if (!invocation.values.contains(colorPin.id))
        {
            Expression texture, sampler;
            if (node.definition == "ShaderNodeTexImage")
            {
                Properties(invocation, node, {"image", "colorSpace", "interpolation", "extension", "projection"});
                if (node.properties.at("projection") != "FLAT")
                    Fail(invocation, node, &socket, "material_texture_projection", "Only FLAT projection is supported",
                         "projection");
                const auto& interpolation = node.properties.at("interpolation");
                if (interpolation != "Linear" && interpolation != "Closest")
                    Fail(invocation, node, &socket, "material_texture_interpolation",
                         "Cubic/Smart interpolation is unsupported", "interpolation");
                const auto& extension = node.properties.at("extension");
                if (extension != "REPEAT" && extension != "EXTEND")
                    Fail(invocation, node, &socket, "material_texture_extension",
                         "CLIP/MIRROR extension is unsupported", "extension");
                texture =
                    Resource(LXMaterialResourceKind::Texture, node.properties.at("image"), 0,
                             Encoding(node.properties.at("colorSpace")), Source(invocation, node, &socket, "image"));
                sampler = Resource(LXMaterialResourceKind::Sampler,
                                   std::string(interpolation == "Linear" ? "linear-" : "nearest-") +
                                       (extension == "REPEAT" ? "repeat" : "clamp"),
                                   0, LXColorSpace::Data, Source(invocation, node, &socket));
            }
            else
            {
                Properties(invocation, node, {"colorSpace"});
                auto textureValue = Input(invocation, node, "Texture"),
                     samplerValue = Input(invocation, node, "Sampler");
                texture = textureValue.constant
                              ? Resource(LXMaterialResourceKind::Texture, textureValue.text, 0,
                                         Encoding(node.properties.at("colorSpace")), Source(invocation, node, &socket))
                              : textureValue;
                sampler = samplerValue.constant ? Resource(LXMaterialResourceKind::Sampler, samplerValue.text, 0,
                                                           LXColorSpace::Data, Source(invocation, node, &socket))
                                                : samplerValue;
            }
            const auto& vectorPin = Socket(node, "Vector", Direction::Input);
            auto vector = Input(invocation, node, "Vector");
            std::string uv = vector.text;
            const bool interfaceInput =
                invocation.parent && std::ranges::any_of(invocation.scope->groupInterface, [&](const auto& item) {
                    return item.direction == Direction::Input && item.internalPin == vectorPin.id;
                });
            if (!interfaceInput && !Incoming(invocation, vectorPin.id) && vector.constant &&
                std::get<std::array<double, 3>>(*vector.constant) == std::array<double, 3>{})
                uv = "context.uv";
            const auto name = Name(invocation, node) + "_sample";
            Emit("float4 " + name + " = LXSampleMaterialImage(" + texture.text + ", " + sampler.text + ", (" + uv +
                     ").xy, context.lod);",
                 Source(invocation, node, &socket));
            ++program_.textureSamples;
            invocation.values[colorPin.id] = {PinType::Color, name};
            const auto& alpha = Socket(node, "Alpha", Direction::Output);
            invocation.values[alpha.id] = {PinType::Float, name + ".a"};
        }
        return invocation.values.at(socket.id);
    }

    Expression Principled(Invocation& invocation, const LXMaterialIRNode& node, const LXMaterialIRSocket& socket)
    {
        Properties(invocation, node, {});
        for (const char* name : {"Diffuse Roughness", "Weight"})
            if (!Zero(Input(invocation, node, name)))
                Fail(invocation, node, &Socket(node, name, Direction::Input), "material_unsupported_socket",
                     std::string(name) + " is unsupported");
        program_.features |= 0x004Fu;
        const auto name = Name(invocation, node);
        Emit("MaterialInputs " + name + " = DefaultMaterialInputs();", Source(invocation, node, &socket));
        const auto assign = [&](const char* socketName, const char* field, bool rgb = false) {
            const auto& pin = Socket(node, socketName, Direction::Input);
            auto value = Resolve(invocation, pin.id);
            Emit(name + "." + field + " = " + value.text + (rgb ? ".rgb" : "") + ";", Source(invocation, node, &pin));
            return value;
        };
        assign("Base Color", "baseColor", true);
        const auto metallic = assign("Metallic", "metallic");
        assign("Roughness", "roughness");
        assign("IOR", "ior");
        assign("Alpha", "alpha");
        assign("Normal", "normal");
        assign("Specular IOR Level", "specularIorLevel");
        assign("Specular Tint", "specularTint", true);
        const auto layer = [&](const char* weight, const char* field, std::uint32_t bit) {
            const auto value = Input(invocation, node, weight);
            if (!Active(value))
                return false;
            program_.features |= bit;
            assign(weight, field);
            return true;
        };
        if (layer("Coat Weight", "coatWeight", 0x80u))
        {
            assign("Coat Roughness", "coatRoughness");
            assign("Coat IOR", "coatIor");
            assign("Coat Tint", "coatTint", true);
            assign("Coat Normal", "coatNormal");
        }
        if (layer("Sheen Weight", "sheenWeight", 0x100u))
        {
            assign("Sheen Roughness", "sheenRoughness");
            assign("Sheen Tint", "sheenTint", true);
        }
        if (layer("Anisotropic", "anisotropy", 0x200u))
        {
            assign("Anisotropic Rotation", "anisotropyRotation");
            assign("Tangent", "tangent");
        }
        if (layer("Thin Film Thickness", "thinFilmThickness", 0x400u))
            assign("Thin Film IOR", "thinFilmIor");
        const bool fullMetal = Scalar(metallic) && *Scalar(metallic) >= 1;
        bool fullGlass = false;
        if (!fullMetal && layer("Transmission Weight", "transmissionWeight", 0x800u))
        {
            const auto weight = Input(invocation, node, "Transmission Weight");
            fullGlass = Scalar(weight) && *Scalar(weight) >= 1;
        }
        if (!fullMetal && !fullGlass && layer("Subsurface Weight", "subsurfaceWeight", 0x1000u))
        {
            assign("Subsurface Radius", "subsurfaceRadius");
            assign("Subsurface Scale", "subsurfaceScale");
            assign("Subsurface IOR", "subsurfaceIor");
            assign("Subsurface Anisotropy", "subsurfaceAnisotropy");
        }
        if (Active(Input(invocation, node, "Emission Strength")))
        {
            program_.features |= 0x10u;
            assign("Emission Strength", "emissionStrength");
            assign("Emission Color", "emissionColor", true);
        }
        return {PinType::Closure, name, std::nullopt, Closure::Surface};
    }

    Expression Operator(Invocation& invocation, const LXMaterialIRNode& node, const LXMaterialIRSocket& socket)
    {
        if (node.definition.starts_with("LXParameter"))
            return Parameter(invocation, node, socket);
        if (node.definition.starts_with("LXReroute"))
        {
            Properties(invocation, node, {});
            return Input(invocation, node, "Input");
        }
        if (node.definition == "ShaderNodeRGB" || node.definition == "ShaderNodeValue")
        {
            Properties(invocation, node, {});
            return Literal(socket.value, socket.type, socket.state.colorSpace, Source(invocation, node, &socket));
        }
        if (node.definition == "ShaderNodeTexImage" || node.definition == "LXTextureSample")
            return Sample(invocation, node, socket);
        if (node.definition == "ShaderNodeBsdfPrincipled")
            return Principled(invocation, node, socket);
        if (node.definition == "ShaderNodeSeparateColor")
        {
            Properties(invocation, node, {});
            const auto color = Input(invocation, node, "Color");
            const auto channel = socket.identifier == "Red" ? "r" : socket.identifier == "Green" ? "g" : "b";
            return {PinType::Float, "(" + color.text + ")." + channel};
        }
        if (node.definition == "LXTextureCoordinates")
        {
            Properties(invocation, node, {});
            const auto offset = Input(invocation, node, "Offset");
            const auto scale = Input(invocation, node, "Scale");
            const auto rotation = Input(invocation, node, "Rotation");
            return {PinType::Vector, "float3((" + offset.text + ").xy + mul(float2x2(cos(" + rotation.text +
                                         "), -sin(" + rotation.text + "), sin(" + rotation.text + "), cos(" +
                                         rotation.text + ")), context.uv.xy * (" + scale.text + ").xy), 0.0)"};
        }
        if (node.definition == "LXSurfaceSettings")
        {
            program_.features |= 0x20u;
            Properties(invocation, node, {"alphaMode"});
            const auto surface = Input(invocation, node, "Surface");
            if (surface.closure != Closure::Surface)
                Fail(invocation, node, &socket, "material_surface_settings",
                     "Surface Settings needs a surface closure");
            const auto name = Name(invocation, node);
            Emit("MaterialInputs " + name + " = " + surface.text + ";", Source(invocation, node, &socket));
            const auto occlusion = Input(invocation, node, "Occlusion");
            const auto strength = Input(invocation, node, "Strength");
            Emit(name + ".occlusion = lerp(1.0, " + occlusion.text + ", saturate(" + strength.text + "));",
                 Source(invocation, node, &socket));
            const auto mode = node.properties.at("alphaMode");
            if (mode == "opaque")
                Emit(name + ".alpha = 1.0;", Source(invocation, node, &socket));
            else if (mode == "masked")
                Emit(name + ".alpha = step(" + Input(invocation, node, "Alpha Cutoff").text + ", " + name + ".alpha);",
                     Source(invocation, node, &socket));
            else if (mode != "transparent")
                Fail(invocation, node, &socket, "material_alpha_mode", "Unknown material alpha mode", "alphaMode");
            return {PinType::Closure, name, std::nullopt, Closure::Surface};
        }
        if (node.definition == "LXPrincipledVolume")
        {
            Properties(invocation, node, {});
            const auto name = Name(invocation, node);
            Emit("VolumeInputs " + name + " = DefaultVolumeInputs();", Source(invocation, node, &socket));
            const std::pair<const char*, const char*> fields[] = {{"Color", "color"},
                                                                  {"Density", "density"},
                                                                  {"Anisotropy", "anisotropy"},
                                                                  {"Absorption Color", "absorptionColor"},
                                                                  {"Emission Strength", "emissionStrength"}};
            for (const auto& [identifier, field] : fields)
            {
                const auto& pin = Socket(node, identifier, Direction::Input);
                Emit(name + "." + field + " = " + Resolve(invocation, pin.id).text +
                         (pin.type == PinType::Color ? ".rgb" : "") + ";",
                     Source(invocation, node, &pin));
            }
            if (Active(Input(invocation, node, "Emission Strength")))
            {
                const auto& pin = Socket(node, "Emission Color", Direction::Input);
                Emit(name + ".emissionColor = " + Resolve(invocation, pin.id).text + ".rgb;",
                     Source(invocation, node, &pin));
            }
            return {PinType::Closure, name, std::nullopt, Closure::Volume};
        }
        if (node.definition == "LXMultiplyFloat" || node.definition == "LXMultiplyColor")
        {
            Properties(invocation, node, {});
            auto a = Input(invocation, node, "A"), b = Input(invocation, node, "B");
            if (a.constant && b.constant)
            {
                LXSocketValue value;
                if (socket.type == PinType::Float)
                    value = std::get<double>(*a.constant) * std::get<double>(*b.constant);
                else
                {
                    auto color = std::get<std::array<double, 4>>(*a.constant);
                    const auto other = std::get<std::array<double, 4>>(*b.constant);
                    for (std::size_t index = 0; index < 4; ++index)
                        color[index] *= other[index];
                    value = color;
                }
                return Literal(value, socket.type, LXColorSpace::Linear, Source(invocation, node, &socket));
            }
            const auto name = Name(invocation, node);
            Emit(TypeName(socket.type) + " " + name + " = " + a.text + " * " + b.text + ";",
                 Source(invocation, node, &socket));
            return {socket.type, name};
        }
        if (node.definition == "ShaderNodeNormalMap")
        {
            Properties(invocation, node, {"space", "uvMap"});
            if (!node.properties.at("uvMap").empty())
                Fail(invocation, node, &socket, "material_normal_uv", "Named UV maps are unsupported", "uvMap");
            const auto& space = node.properties.at("space");
            if (space != "TANGENT" && space != "WORLD")
                Fail(invocation, node, &socket, "material_normal_space", "Only TANGENT and WORLD spaces are supported",
                     "space");
            const auto strength = Input(invocation, node, "Strength");
            if (!Active(strength))
                return {PinType::Vector, "context.normal"};
            const auto color = Input(invocation, node, "Color");
            const auto name = Name(invocation, node);
            Emit("float3 " + name + " = LXGeneratedNormal(context, " + color.text + ".rgb, " + strength.text + ", " +
                     (space == "TANGENT" ? "true" : "false") + ");",
                 Source(invocation, node, &socket));
            return {PinType::Vector, name};
        }
        Fail(invocation, node, &socket, "material_unsupported_operator", "No Slang lowering for " + node.definition);
    }

    void Append(const std::string& text, const LXMaterialSource* source = nullptr)
    {
        const auto lines = static_cast<std::uint32_t>(std::ranges::count(text, '\n')) + 1;
        if (source)
            program_.sourceMap.push_back({line_, line_ + lines - 1, *source});
        program_.slang += text + '\n';
        line_ += lines;
    }

    void Render(const Expression& surface, const Expression& volume)
    {
        Append("// Generated by LX Material Compiler v2. UTF-8/LF; layout is excluded.");
        Append("#define LX_MATERIAL_FEATURE_MASK " + std::to_string(program_.features) + "u");
        if (program_.volume)
        {
            Append(std::string("#define LX_MATERIAL_VOLUME_HOMOGENEOUS ") +
                   (contextDependent_.contains(volume.text) ? "0" : "1"));
        }
        Append("#include \"PrincipledSurface.slang\"");
        if (program_.execution.forward)
            Append("#include \"SpecialMaterialRoute.slang\"");
        if (program_.volume)
            Append("#include \"PrincipledVolume.slang\"");
        Append("#include \"TangentFrame.slang\"");
        Append("#include \"MaterialGraphTextureSample.slang\"");
        Append(R"(
struct LXMaterialContext
{
    float3 uv;
    float lod;
    float3 normal;
    float3 tangent;
    float3 bitangent;
};

float4 LXDecodeSrgb(float4 color)
{
    const float3 linear = select(color.rgb <= 0.04045, color.rgb / 12.92,
        pow(max((color.rgb + 0.055) / 1.055, 0.0), 2.4));
    return float4(linear, color.a);
}

float3 LXGeneratedNormal(LXMaterialContext context, float3 color, float strength, bool tangentSpace)
{
    const float3 n = PrincipledUnitNormal(context.normal, float3(0, 0, 1));
    const float3 decoded = color * 2.0 - 1.0;
    const float scale = max(strength, 0.0);
    if (!tangentSpace)
    {
        return PrincipledUnitNormal(lerp(n, decoded, scale), n);
    }
    const TangentFrame frame = BuildTangentFrame(n, context.tangent, context.bitangent);
    if (frame.degenerate)
    {
        return n;
    }
    const float3 perturbed = float3(decoded.xy * scale, lerp(1.0, decoded.z, saturate(scale)));
    return PrincipledUnitNormal(mul(perturbed, frame.basis), n);
}
)");
        Append("struct LXMaterialParameters\n{");
        bool numeric = false;
        for (const auto& [id, parameter] : parameters_)
        {
            program_.parameters.push_back(parameter);
            if (parameter.type == PinType::Texture || parameter.type == PinType::Sampler)
                continue;
            numeric = true;
            Append("    " + TypeName(parameter.type) + " lx_p" + std::to_string(id) + ";");
        }
        if (!numeric)
            Append("    float reserved;");
        Append("};\n\nLXMaterialParameters LXDefaultMaterialParameters()\n{\n    LXMaterialParameters parameters;");
        if (!numeric)
            Append("    parameters.reserved = 0.0;");
        for (const auto& [id, parameter] : parameters_)
        {
            if (parameter.type == PinType::Texture || parameter.type == PinType::Sampler)
                continue;
            Append("    parameters.lx_p" + std::to_string(id) + " = " +
                   Literal(parameter.value, parameter.type, LXColorSpace::Data, {}).text + ";");
        }
        Append("    return parameters;\n}\n");
        for (const auto& resource : program_.resources)
        {
            const bool texture = resource.kind == LXMaterialResourceKind::Texture;
            Append(std::string(texture ? "Texture2D<float4> lx_texture_" : "SamplerState lx_sampler_") +
                       std::to_string(resource.slot) + " : register(" + (texture ? "t" : "s") +
                       std::to_string(resource.slot) + ", space1);",
                   &resource.source);
        }
        Append("\nstruct LXGeneratedMaterial\n{\n    MaterialInputs surfaceInputs;");
        if (program_.volume)
            Append("    VolumeInputs volumeInputs;");
        Append("};\n\nLXGeneratedMaterial LXGenerateMaterial(LXMaterialContext context, LXMaterialParameters "
               "parameters)\n{");
        for (const auto& statement : statements_)
            Append("    " + statement.text, &statement.source);
        Append("    LXGeneratedMaterial result;");
        if (program_.surface)
            Append("    result.surfaceInputs = " + surface.text + ";");
        else
            Append("    result.surfaceInputs = DefaultMaterialInputs();\n    result.surfaceInputs.alpha = 0.0;");
        if (program_.volume)
            Append("    result.volumeInputs = " + volume.text + ";");
        Append("    return result;\n}\n");
        Append(
            "PrincipledSurface LXEvaluateGeneratedSurface(LXGeneratedMaterial inputs, LXMaterialContext context)\n{\n"
            "    return EvaluateMaterial(inputs.surfaceInputs, context.normal, context.tangent);\n}\n");
        if (program_.volume)
            Append("PrincipledVolume LXEvaluateGeneratedVolume(LXGeneratedMaterial inputs)\n{\n"
                   "    return EvaluateVolume(inputs.volumeInputs);\n}\n");
    }

    void CanonicalKey()
    {
        std::string& key = program_.semanticKey;
        const auto field = [&](const std::string& text) { key += std::to_string(text.size()) + ":" + text; };
        field("lx.material.compiler/2;common-abi/MAT-5");
        field(options_.dependencies);
        field(program_.slang);
        for (const auto& resource : program_.resources)
        {
            field(std::to_string(int(resource.kind)));
            field(std::to_string(resource.slot));
            field(std::to_string(resource.parameter));
            field(resource.reference);
            field(std::to_string(int(resource.colorSpace)));
        }
        for (const auto& parameter : program_.parameters)
        {
            field(std::to_string(parameter.id));
            field(parameter.identifier);
            field(std::to_string(int(parameter.type)));
            field(std::to_string(int(parameter.colorSpace)));
            field(parameter.exposed ? "1" : "0");
        }
    }
};
} // namespace

std::optional<LXMaterialProgram> GenerateMaterialSlang(const LXMaterialAsset& asset,
                                                       std::vector<LXMaterialDiagnostic>* diagnostics,
                                                       LXMaterialCompilerOptions options)
{
    std::vector<LXMaterialDiagnostic> errors;
    std::vector<Issue> issues;
    auto ir = BuildMaterialIR(asset, &issues);
    if (!ir)
    {
        for (const auto& issue : issues)
            errors.push_back({issue.code, issue.message, {issue.scope, issue.node, issue.pin}, issue.severity});
    }
    else
    {
        try
        {
            auto result = Generator(*ir, options).Run();
            if (diagnostics)
                diagnostics->clear();
            return result;
        }
        catch (const GenerationError& error)
        {
            errors.push_back(error.diagnostic);
        }
        catch (const std::exception& error)
        {
            errors.push_back({"material_codegen", error.what()});
        }
    }
    if (diagnostics)
        *diagnostics = std::move(errors);
    return std::nullopt;
}

std::optional<LXMaterialSource> FindMaterialSource(const LXMaterialProgram& program, std::uint32_t line)
{
    for (const auto& range : program.sourceMap)
        if (line >= range.firstLine && line <= range.lastLine)
            return range.source;
    return std::nullopt;
}

std::string WriteMaterialProgramMetadata(const LXMaterialProgram& program)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    const auto quote = [&](const std::string& text) {
        out << '"';
        for (unsigned char character : text)
        {
            switch (character)
            {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (character < 0x20)
                    out << "\\u00" << std::hex << std::setfill('0') << std::setw(2) << unsigned(character) << std::dec;
                else
                    out << character;
            }
        }
        out << '"';
    };
    const auto source = [&](const LXMaterialSource& location) {
        out << "{\"scope\":" << location.scope << ",\"node\":" << location.node << ",\"pin\":" << location.pin
            << ",\"instances\":[";
        for (std::size_t index = 0; index < location.instances.size(); ++index)
            out << (index ? "," : "") << location.instances[index];
        out << "],\"property\":";
        quote(location.property);
        out << '}';
    };
    out << "{\n  \"kind\":\"LatticeMaterialProgram\",\n  \"schemaVersion\":1,\n  \"compilerVersion\":"
        << LXMaterialProgram::CompilerVersion << ",\n  \"features\":" << program.features
        << ",\n  \"surface\":" << (program.surface ? "true" : "false")
        << ",\n  \"volume\":" << (program.volume ? "true" : "false")
        << ",\n  \"textureSamples\":" << program.textureSamples
        << ",\n  \"requirements\":{\"forward\":" << (program.execution.forward ? "true" : "false")
        << ",\"refraction\":" << (program.execution.refraction ? "true" : "false")
        << ",\"subsurface\":" << (program.execution.subsurface ? "true" : "false")
        << ",\"volume\":" << (program.execution.volume ? "true" : "false") << "},\n  \"resources\":[";
    for (std::size_t index = 0; index < program.resources.size(); ++index)
    {
        const auto& resource = program.resources[index];
        const bool texture = resource.kind == LXMaterialResourceKind::Texture;
        out << (index ? ",\n" : "\n") << "    {\"kind\":\"" << (texture ? "Texture" : "Sampler")
            << "\",\"shaderName\":";
        quote((texture ? "lx_texture_" : "lx_sampler_") + std::to_string(resource.slot));
        out << ",\"register\":" << resource.slot << ",\"space\":1,\"parameter\":" << resource.parameter
            << ",\"reference\":";
        quote(resource.reference);
        out << ",\"colorSpace\":\""
            << (resource.colorSpace == LXColorSpace::SRGB     ? "srgb"
                : resource.colorSpace == LXColorSpace::Linear ? "linear"
                                                              : "data")
            << "\",\"source\":";
        source(resource.source);
        out << '}';
    }
    out << "\n  ],\n  \"parameters\":[";
    for (std::size_t index = 0; index < program.parameters.size(); ++index)
    {
        const auto& parameter = program.parameters[index];
        out << (index ? ",\n" : "\n") << "    {\"id\":" << parameter.id << ",\"identifier\":";
        quote(parameter.identifier);
        out << ",\"name\":";
        quote(parameter.name);
        out << ",\"type\":";
        quote(PinTypeName(parameter.type));
        out << ",\"default\":";
        std::visit(
            [&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::monostate>)
                    out << "null";
                else if constexpr (std::is_same_v<T, bool>)
                    out << (value ? "true" : "false");
                else if constexpr (std::is_same_v<T, std::string>)
                    quote(value);
                else if constexpr (std::is_arithmetic_v<T>)
                    out << value;
                else
                {
                    out << '[';
                    for (std::size_t component = 0; component < value.size(); ++component)
                        out << (component ? "," : "") << value[component];
                    out << ']';
                }
            },
            parameter.value);
        out << ",\"colorSpace\":\""
            << (parameter.colorSpace == LXColorSpace::SRGB     ? "srgb"
                : parameter.colorSpace == LXColorSpace::Linear ? "linear"
                                                               : "data")
            << "\",\"exposed\":" << (parameter.exposed ? "true" : "false") << '}';
    }
    out << "\n  ],\n  \"sourceMap\":[";
    for (std::size_t index = 0; index < program.sourceMap.size(); ++index)
    {
        const auto& range = program.sourceMap[index];
        out << (index ? ",\n" : "\n") << "    {\"firstLine\":" << range.firstLine << ",\"lastLine\":" << range.lastLine
            << ",\"source\":";
        source(range.source);
        out << '}';
    }
    out << "\n  ]\n}\n";
    return out.str();
}

std::vector<LXMaterialDiagnostic> MapMaterialCompilerDiagnostics(const LXMaterialProgram& program,
                                                                 const std::filesystem::path& sourceFile,
                                                                 const std::string& text)
{
    const std::regex location(
        R"(^(.+?)(?:\(([0-9]+)(?:,[0-9]+)?\)|:([0-9]+)(?::[0-9]+)?):\s*(warning|error)[^:]*:\s*(.*)$)");
    const std::regex header(R"(^(?:fatal )?(warning|error)(?:\[[^\]]+\])?[^:]*:\s*(.*)$)");
    const std::regex arrow(R"(^\s*-->\s+(.+):([0-9]+):([0-9]+)\s*$)");
    std::vector<LXMaterialDiagnostic> result;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
    {
        std::smatch match;
        if (!std::regex_match(line, match, location))
        {
            if (std::regex_match(line, match, header))
            {
                result.push_back({"material_backend",
                                  line,
                                  {},
                                  match[1] == "warning" ? Issue::Severity::Warning : Issue::Severity::Error});
            }
            else if (!result.empty())
            {
                result.back().message += '\n' + line;
                if (std::regex_match(line, match, arrow))
                {
                    const std::filesystem::path file(match[1].str());
                    if (file.lexically_normal() == sourceFile.lexically_normal() ||
                        (!file.has_parent_path() && file == sourceFile.filename()))
                    {
                        const auto number = match[2].str();
                        std::uint32_t index = 0;
                        const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
                        if (parsed.ec == std::errc{})
                            result.back().source = FindMaterialSource(program, index).value_or(LXMaterialSource{});
                    }
                }
            }
            continue;
        }
        LXMaterialDiagnostic diagnostic{"material_backend", line};
        diagnostic.severity = match[4] == "warning" ? Issue::Severity::Warning : Issue::Severity::Error;
        const std::filesystem::path file(match[1].str());
        if (file.lexically_normal() == sourceFile.lexically_normal() ||
            (!file.has_parent_path() && file == sourceFile.filename()))
        {
            const auto number = match[2].matched ? match[2].str() : match[3].str();
            std::uint32_t index = 0;
            const auto parsed = std::from_chars(number.data(), number.data() + number.size(), index);
            if (parsed.ec == std::errc{})
                diagnostic.source = FindMaterialSource(program, index).value_or(LXMaterialSource{});
        }
        result.push_back(std::move(diagnostic));
    }
    if (result.empty() && !text.empty())
        result.push_back({"material_backend", text});
    return result;
}

bool LXMaterialProgramStore::Publish(const LXMaterialAsset& asset, const LXMaterialVerifier& verifier,
                                     std::vector<LXMaterialDiagnostic>* diagnostics, LXMaterialCompilerOptions options)
{
    std::vector<LXMaterialDiagnostic> errors;
    auto candidate = GenerateMaterialSlang(asset, &errors, options);
    if (!candidate)
    {
        if (diagnostics)
            *diagnostics = std::move(errors);
        return false;
    }
    if (program_ && program_->semanticKey == candidate->semanticKey)
    {
        // Source locations are authoring metadata and may change without recompilation.
        program_ = std::move(candidate);
        if (diagnostics)
            diagnostics->clear();
        return true;
    }
    std::vector<LXMaterialShaderArtifact> artifacts;
    bool verified = false;
    try
    {
        verified = verifier && verifier(*candidate, artifacts, errors);
    }
    catch (const std::exception& error)
    {
        errors.push_back({"material_backend", error.what()});
    }
    std::set<std::pair<std::string, std::string>> targets;
    if (verified)
    {
        for (const auto& artifact : artifacts)
            if (artifact.backend.empty() || artifact.entryPoint.empty() || artifact.bytecode.empty() ||
                !targets.emplace(artifact.backend, artifact.entryPoint).second)
                verified = false;
        verified = verified && !artifacts.empty() && std::ranges::none_of(errors, [](const auto& error) {
                       return error.severity == Issue::Severity::Error;
                   });
    }
    if (!verified)
    {
        if (std::ranges::none_of(errors, [](const auto& error) { return error.severity == Issue::Severity::Error; }))
            errors.push_back({"material_backend", "Backend validation did not produce a complete artifact set"});
        if (diagnostics)
            *diagnostics = std::move(errors);
        return false;
    }
    program_ = std::move(candidate);
    artifacts_ = std::move(artifacts);
    ++generation_;
    if (diagnostics)
        *diagnostics = std::move(errors);
    return true;
}
} // namespace LX
