#include "LXMaterialGraph.h"

#include <ryml/ryml.hpp>
#include <c4/std/string.hpp>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <Windows.h>

namespace LX
{
namespace
{
constexpr std::size_t MaxArchiveBytes = 16 * 1024 * 1024;
constexpr std::size_t MaxEntries = 100000;
std::atomic_uint64_t saveSequence = 0;

class ArchiveError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// Unsupported versions/domains/schemas must not silently fall back to an older backup.
class CompatibilityError : public ArchiveError
{
  public:
    using ArchiveError::ArchiveError;
};

std::string Text(ryml::csubstr value)
{
    return std::string(value.str, value.len);
}

ryml::Callbacks ArchiveCallbacks()
{
    ryml::Callbacks callbacks;
    callbacks.set_error_basic(
        [](ryml::csubstr message, const ryml::ErrorDataBasic&, void*) { throw ArchiveError(Text(message)); });
    callbacks.set_error_parse(
        [](ryml::csubstr message, const ryml::ErrorDataParse&, void*) { throw ArchiveError(Text(message)); });
    callbacks.set_error_visit(
        [](ryml::csubstr message, const ryml::ErrorDataVisit&, void*) { throw ArchiveError(Text(message)); });
    return callbacks;
}

void CheckTree(ryml::ConstNodeRef node, unsigned depth = 0)
{
    if (depth > 64 || node.has_key_tag() || node.has_val_tag() || node.is_key_ref() || node.is_val_ref())
    {
        throw ArchiveError("Archive nesting, tags or aliases are unsupported");
    }
    std::set<std::string> keys;
    for (auto child : node.children())
    {
        if (node.is_map() && !keys.insert(Text(child.key())).second)
        {
            throw ArchiveError("Duplicate archive field");
        }
        CheckTree(child, depth + 1);
    }
}

ryml::Tree ParseTree(const std::string& text)
{
    if (text.size() > MaxArchiveBytes)
    {
        throw ArchiveError("Archive exceeds the size limit");
    }
    ryml::Tree tree(ArchiveCallbacks());
    ryml::parse_in_arena(ryml::csubstr(text.data(), text.size()), &tree);
    if (tree.size() > MaxEntries || !tree.rootref().is_map())
    {
        throw ArchiveError("Invalid archive root or entry limit");
    }
    CheckTree(tree.rootref());
    return tree;
}

ryml::ConstNodeRef Field(ryml::ConstNodeRef node, const char* key)
{
    if (!node.is_map() || !node.has_child(ryml::to_csubstr(key)))
    {
        throw ArchiveError(std::string("Missing field: ") + key);
    }
    return node[ryml::to_csubstr(key)];
}

std::string String(ryml::ConstNodeRef node)
{
    if (!node.has_val() || node.val_is_null())
    {
        throw ArchiveError("Expected a scalar string");
    }
    return Text(node.val());
}

void Fields(ryml::ConstNodeRef node, std::initializer_list<const char*> allowed)
{
    if (!node.is_map())
    {
        throw ArchiveError("Expected an archive record");
    }
    for (auto child : node.children())
    {
        const auto key = Text(child.key());
        if (std::ranges::none_of(allowed, [&](const char* name) { return key == name; }))
        {
            throw CompatibilityError("Unrecognized archive field: " + key);
        }
    }
}

template<typename T>
T Number(ryml::ConstNodeRef node)
{
    const auto text = String(node);
    T result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw ArchiveError("Invalid numeric field");
    }
    if constexpr (std::is_floating_point_v<T>)
    {
        if (!std::isfinite(result))
        {
            throw ArchiveError("Non-finite numeric field");
        }
    }
    return result;
}

bool Boolean(ryml::ConstNodeRef node)
{
    const auto text = String(node);
    if (text != "true" && text != "false")
    {
        throw ArchiveError("Invalid boolean field");
    }
    return text == "true";
}

ryml::ConstNodeRef Sequence(ryml::ConstNodeRef node)
{
    if (!node.is_seq() || node.num_children() > MaxEntries)
    {
        throw ArchiveError("Invalid sequence");
    }
    return node;
}

PinType ReadType(ryml::ConstNodeRef node)
{
    const auto name = String(node);
    for (unsigned index = 0; index <= static_cast<unsigned>(PinType::Closure); ++index)
    {
        const auto type = static_cast<PinType>(index);
        if (name == PinTypeName(type))
        {
            return type;
        }
    }
    throw CompatibilityError("Unknown socket type: " + name);
}

Direction ReadDirection(ryml::ConstNodeRef node)
{
    const auto name = String(node);
    if (name != "input" && name != "output")
    {
        throw ArchiveError("Invalid socket direction");
    }
    return name == "input" ? Direction::Input : Direction::Output;
}

const char* SpaceName(LXColorSpace space)
{
    switch (space)
    {
    case LXColorSpace::Data:
        return "data";
    case LXColorSpace::Linear:
        return "linear";
    case LXColorSpace::SRGB:
        return "srgb";
    }
    throw ArchiveError("Invalid color-space intent");
}

LXColorSpace ReadSpace(ryml::ConstNodeRef node)
{
    const auto name = String(node);
    if (name == "data")
    {
        return LXColorSpace::Data;
    }
    if (name == "linear")
    {
        return LXColorSpace::Linear;
    }
    if (name == "srgb")
    {
        return LXColorSpace::SRGB;
    }
    throw CompatibilityError("Unknown color-space intent: " + name);
}

LXSocketValue ReadValue(ryml::ConstNodeRef node)
{
    Fields(node, {"kind", "value"});
    const auto kind = String(Field(node, "kind"));
    if (kind == "none")
    {
        return {};
    }
    const auto value = Field(node, "value");
    if (kind == "bool")
    {
        return Boolean(value);
    }
    if (kind == "int")
    {
        return Number<std::int64_t>(value);
    }
    if (kind == "float")
    {
        return Number<double>(value);
    }
    if (kind == "string")
    {
        return String(value);
    }
    if (kind == "vector" || kind == "color")
    {
        Sequence(value);
        if (value.num_children() != (kind == "vector" ? 3u : 4u))
        {
            throw ArchiveError("Invalid socket component count");
        }
        std::array<double, 4> components{};
        for (std::size_t index = 0; index < value.num_children(); ++index)
        {
            components[index] = Number<double>(value[index]);
        }
        if (kind == "vector")
        {
            return std::array<double, 3>{components[0], components[1], components[2]};
        }
        return components;
    }
    throw CompatibilityError("Unknown socket value kind: " + kind);
}

std::string Quote(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (unsigned char character : value)
    {
        if (character == '"' || character == '\\')
        {
            out << '\\' << character;
        }
        else if (character < 32)
        {
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(character);
        }
        else
        {
            out << character;
        }
    }
    out << '"';
    return out.str();
}

void WriteValue(std::ostream& out, const LXSocketValue& value)
{
    static constexpr const char* kinds[] = {"none", "bool", "int", "float", "vector", "color", "string", "vector2"};
    out << "{\"kind\":" << Quote(kinds[value.index()]);
    std::visit(
        [&](const auto& field) {
            using T = std::decay_t<decltype(field)>;
            if constexpr (!std::is_same_v<T, std::monostate>)
            {
                out << ",\"value\":";
                if constexpr (std::is_same_v<T, std::string>)
                {
                    out << Quote(field);
                }
                else if constexpr (std::is_same_v<T, std::array<double, 2>> ||
                                   std::is_same_v<T, std::array<double, 3>> || std::is_same_v<T, std::array<double, 4>>)
                {
                    out << '[';
                    for (std::size_t index = 0; index < field.size(); ++index)
                    {
                        out << (index ? "," : "") << field[index];
                    }
                    out << ']';
                }
                else
                {
                    out << field;
                }
            }
        },
        value);
    out << '}';
}

void WriteSocketState(std::ostream& out, const LXMaterialSocketState& state)
{
    out << ",\"enabled\":" << state.enabled << ",\"hidden\":" << state.hidden << ",\"hideValue\":" << state.hideValue
        << ",\"colorSpace\":" << Quote(SpaceName(state.colorSpace));
}

void WriteGraph(std::ostream& out, const LXMaterialAsset& asset, const LXGraph& graph, Id scope, unsigned depth)
{
    const std::string indent(depth * 2, ' ');
    out << "{\n"
        << indent << "  \"domain\":" << Quote(graph.Domain()) << ",\"nextId\":" << graph.NextId() << ",\n"
        << indent << "  \"nodes\":[";
    bool first = true;
    for (const Node& node : graph.Nodes())
    {
        out << (first ? "\n" : ",\n") << indent << "    ";
        first = false;
        const auto state = asset.NodeState(scope, node);
        if (!state.unknownPayload.empty())
        {
            out << state.unknownPayload;
            continue;
        }
        out << "{\"id\":" << node.id << ",\"definition\":" << Quote(node.type)
            << ",\"schemaRevision\":" << state.schemaRevision << ",\"title\":" << Quote(node.title)
            << ",\"groupId\":" << node.groupId << ",\"sockets\":[";
        bool firstPin = true;
        for (const Pin& pin : node.pins)
        {
            out << (firstPin ? "\n" : ",\n") << indent << "      {\"id\":" << pin.id
                << ",\"identifier\":" << Quote(pin.Identifier()) << ",\"name\":" << Quote(pin.name)
                << ",\"direction\":" << Quote(pin.direction == Direction::Input ? "input" : "output")
                << ",\"type\":" << Quote(PinTypeName(pin.type)) << ",\"multiple\":" << pin.multiple
                << ",\"dynamic\":" << pin.dynamic << ",\"interfaceId\":" << pin.interfaceId << ",\"default\":";
            firstPin = false;
            WriteValue(out, pin.value);
            WriteSocketState(out, asset.SocketState(scope, pin));
            out << '}';
        }
        out << "],\"properties\":{ ";
        bool firstProperty = true;
        for (const auto& [key, value] : node.properties)
        {
            out << (firstProperty ? "" : ",") << Quote(key) << ':' << Quote(value);
            firstProperty = false;
        }
        out << "},\"dynamicRule\":";
        if (node.dynamicPins)
        {
            const auto& rule = *node.dynamicPins;
            out << "{\"direction\":" << Quote(rule.direction == Direction::Input ? "input" : "output")
                << ",\"type\":" << Quote(PinTypeName(rule.type)) << ",\"multiple\":" << rule.multiple
                << ",\"maxCount\":" << rule.maxCount << '}';
        }
        else
        {
            out << "null";
        }
        out << '}';
    }
    out << "\n" << indent << "  ],\n" << indent << "  \"links\":[";
    first = true;
    for (const Link& link : graph.Links())
    {
        out << (first ? "\n" : ",\n") << indent << "    {\"id\":" << link.id << ",\"output\":" << link.output
            << ",\"input\":" << link.input << '}';
        first = false;
    }
    out << "\n" << indent << "  ],\n" << indent << "  \"groups\":[";
    first = true;
    for (const auto& [id, group] : graph.Groups())
    {
        out << (first ? "\n" : ",\n") << indent << "    {\"id\":" << id << ",\"name\":" << Quote(group.name)
            << ",\"interface\":[";
        first = false;
        bool firstSocket = true;
        for (const auto& socket : group.sockets)
        {
            out << (firstSocket ? "\n" : ",\n") << indent << "      {\"id\":" << socket.id
                << ",\"identifier\":" << Quote(socket.identifier) << ",\"name\":" << Quote(socket.name)
                << ",\"direction\":" << Quote(socket.direction == Direction::Input ? "input" : "output")
                << ",\"type\":" << Quote(PinTypeName(socket.type)) << ",\"internalPin\":" << socket.internalPin
                << ",\"default\":";
            firstSocket = false;
            WriteValue(out, socket.value);
            out << '}';
        }
        out << "],\"body\":";
        WriteGraph(out, asset, *group.body, id, depth + 3);
        out << '}';
    }
    out << "\n" << indent << "  ],\n" << indent << "  \"layout\":{\"nodes\":[";
    first = true;
    for (const auto& [id, placement] : graph.Layout().nodes)
    {
        out << (first ? "\n" : ",\n") << indent << "    {\"id\":" << id << ",\"x\":" << placement.x
            << ",\"y\":" << placement.y << ",\"collapsed\":" << placement.collapsed << ",\"frame\":" << placement.frame
            << '}';
        first = false;
    }
    out << "],\"frames\":[";
    first = true;
    for (const auto& [id, frame] : graph.Layout().frames)
    {
        out << (first ? "\n" : ",\n") << indent << "    {\"id\":" << id << ",\"label\":" << Quote(frame.label)
            << ",\"x\":" << frame.x << ",\"y\":" << frame.y << ",\"width\":" << frame.width
            << ",\"height\":" << frame.height << '}';
        first = false;
    }
    const auto& view = graph.Layout().view;
    out << "],\"view\":{\"saved\":" << view.saved << ",\"centerX\":" << view.centerX << ",\"centerY\":" << view.centerY
        << ",\"zoom\":" << view.zoom << "}}\n"
        << indent << '}';
}

bool HasErrors(const std::vector<Issue>& issues)
{
    return std::ranges::any_of(issues, [](const Issue& issue) { return issue.severity == Issue::Severity::Error; });
}

void CollectGraphs(const LXGraph& graph, std::map<Id, const LXGraph*>& scopes, unsigned depth = 0)
{
    if (depth > 16)
    {
        return;
    }
    for (const auto& [id, group] : graph.Groups())
    {
        if (group.body && scopes.emplace(id, group.body.get()).second)
        {
            CollectGraphs(*group.body, scopes, depth + 1);
        }
    }
}

bool ValidSpace(PinType type, LXColorSpace space)
{
    return static_cast<unsigned>(space) <= static_cast<unsigned>(LXColorSpace::SRGB) &&
           (type == PinType::Color || type == PinType::Texture || space == LXColorSpace::Data);
}

std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
    {
        throw ArchiveError("Cannot open shadergraph");
    }
    const auto length = in.tellg();
    if (length < 0 || length > static_cast<std::streamoff>(MaxArchiveBytes))
    {
        throw ArchiveError("Invalid shadergraph size");
    }
    std::string text(static_cast<std::size_t>(length), '\0');
    in.seekg(0);
    if (!in.read(text.data(), static_cast<std::streamsize>(text.size())))
    {
        throw ArchiveError("Cannot read shadergraph");
    }
    return text;
}

bool Fail(std::string* error, const std::string& message)
{
    if (error)
    {
        *error = message;
    }
    return false;
}
} // namespace

struct LXMaterialArchive::Reader
{
    LXMaterialAsset asset;
    unsigned version = 1;

    explicit Reader(const LXMaterialDefinitions& definitions) : asset(definitions)
    {
        if (!definitions.nodes)
        {
            throw CompatibilityError("Missing Material definition registry");
        }
    }

    LXGraph Graph(ryml::ConstNodeRef source, Id scope, unsigned depth = 0)
    {
        if (depth > 16)
        {
            throw ArchiveError("Group nesting exceeds 16");
        }
        if (String(Field(source, "domain")) != "material")
        {
            throw CompatibilityError("Only material-domain graphs are accepted");
        }
        LXGraph graph("material", asset.Definitions().nodes);
        Fields(source, {"domain", "nextId", "nodes", "links", "groups", "layout"});
        graph.nextId_ = Number<Id>(Field(source, "nextId"));
        Id maximum = 0;
        LXMaterialScopeState metadata;
        for (auto record : Sequence(Field(source, "nodes")).children())
        {
            Node node;
            node.id = Number<Id>(Field(record, "id"));
            node.type = String(Field(record, "definition"));
            node.title = String(Field(record, "title"));
            node.groupId = Number<Id>(Field(record, "groupId"));
            const auto* definition = asset.Definitions().nodes->Find(node.type);
            const auto revision = version == 0 ? 1u : Number<std::uint32_t>(Field(record, "schemaRevision"));
            LXMaterialNodeState state{revision};
            if (!node.groupId && node.type != "LX_GROUP_INPUT" && node.type != "LX_GROUP_OUTPUT" && !definition)
            {
                if (version == 0)
                {
                    throw CompatibilityError(
                        "Schema 0 unknown definitions cannot be migrated without socket identifiers");
                }
                state.unknownPayload = ryml::emitrs_json<std::string>(record);
            }
            metadata.nodes.emplace(node.id, std::move(state));
            if (metadata.nodes.at(node.id).unknownPayload.empty())
            {
                Fields(record, {"id", "definition", "schemaRevision", "title", "groupId", "sockets", "properties",
                                "dynamicRule"});
            }
            maximum = std::max(maximum, node.id);
            for (auto socket : Sequence(Field(record, "sockets")).children())
            {
                if (metadata.nodes.at(node.id).unknownPayload.empty())
                {
                    Fields(socket, {"id", "identifier", "name", "direction", "type", "multiple", "dynamic",
                                    "interfaceId", "default", "enabled", "hidden", "hideValue", "colorSpace"});
                }
                Pin pin;
                pin.id = Number<Id>(Field(socket, "id"));
                pin.node = node.id;
                pin.name = String(Field(socket, "name"));
                pin.identifier = version == 0 ? pin.name : String(Field(socket, "identifier"));
                if (version == 0 && definition && node.pins.size() < definition->defaults.pins.size())
                {
                    pin.identifier = definition->defaults.pins[node.pins.size()].Identifier();
                }
                pin.direction = ReadDirection(Field(socket, "direction"));
                pin.type = ReadType(Field(socket, "type"));
                pin.multiple = Boolean(Field(socket, "multiple"));
                pin.dynamic = Boolean(Field(socket, "dynamic"));
                pin.interfaceId = Number<Id>(Field(socket, "interfaceId"));
                pin.value = ReadValue(Field(socket, "default"));
                LXMaterialSocketState flags;
                flags.enabled = Boolean(Field(socket, "enabled"));
                flags.hidden = Boolean(Field(socket, "hidden"));
                flags.hideValue = Boolean(Field(socket, "hideValue"));
                flags.colorSpace = version == 0
                                       ? (pin.type == PinType::Color ? LXColorSpace::Linear : LXColorSpace::Data)
                                       : ReadSpace(Field(socket, "colorSpace"));
                metadata.sockets.emplace(pin.id, flags);
                maximum = std::max(maximum, pin.id);
                node.pins.push_back(std::move(pin));
            }
            const auto properties = Field(record, "properties");
            if (!properties.is_map())
            {
                throw ArchiveError("Invalid node properties");
            }
            for (auto property : properties.children())
            {
                node.properties.emplace(Text(property.key()), String(property));
            }
            const auto rule = Field(record, "dynamicRule");
            if (!rule.has_val() || !rule.val_is_null())
            {
                Fields(rule, {"direction", "type", "multiple", "maxCount"});
                node.dynamicPins =
                    DynamicPinRule{ReadDirection(Field(rule, "direction")), ReadType(Field(rule, "type")),
                                   Boolean(Field(rule, "multiple")), Number<std::size_t>(Field(rule, "maxCount"))};
            }
            graph.nodes_.push_back(std::move(node));
        }
        for (auto record : Sequence(Field(source, "links")).children())
        {
            Fields(record, {"id", "output", "input"});
            Link link{Number<Id>(Field(record, "id")), Number<Id>(Field(record, "output")),
                      Number<Id>(Field(record, "input"))};
            maximum = std::max(maximum, link.id);
            graph.links_.push_back(link);
        }
        for (auto record : Sequence(Field(source, "groups")).children())
        {
            Fields(record, {"id", "name", "interface", "body"});
            LXGroupDefinition group;
            group.id = Number<Id>(Field(record, "id"));
            group.name = String(Field(record, "name"));
            maximum = std::max(maximum, group.id);
            for (auto socket : Sequence(Field(record, "interface")).children())
            {
                Fields(socket, {"id", "identifier", "name", "direction", "type", "internalPin", "default"});
                LXGroupSocket item;
                item.id = Number<Id>(Field(socket, "id"));
                item.identifier = String(Field(socket, "identifier"));
                item.name = String(Field(socket, "name"));
                item.direction = ReadDirection(Field(socket, "direction"));
                item.type = ReadType(Field(socket, "type"));
                item.internalPin = Number<Id>(Field(socket, "internalPin"));
                item.value = ReadValue(Field(socket, "default"));
                maximum = std::max(maximum, item.id);
                group.sockets.push_back(std::move(item));
            }
            group.body = std::make_shared<LXGraph>(Graph(Field(record, "body"), group.id, depth + 1));
            if (!graph.groups_.emplace(group.id, std::move(group)).second)
            {
                throw ArchiveError("Duplicate group ID");
            }
        }
        const auto layout = Field(source, "layout");
        Fields(layout, {"nodes", "frames", "view"});
        for (auto record : Sequence(Field(layout, "nodes")).children())
        {
            Fields(record, {"id", "x", "y", "collapsed", "frame"});
            const Id id = Number<Id>(Field(record, "id"));
            NodeLayout placement{Number<float>(Field(record, "x")), Number<float>(Field(record, "y")),
                                 Boolean(Field(record, "collapsed")), Number<Id>(Field(record, "frame"))};
            if (!graph.layout_.nodes.emplace(id, placement).second)
            {
                throw ArchiveError("Duplicate layout ID");
            }
        }
        for (auto record : Sequence(Field(layout, "frames")).children())
        {
            Fields(record, {"id", "label", "x", "y", "width", "height"});
            const Id id = Number<Id>(Field(record, "id"));
            FrameLayout frame{String(Field(record, "label")), Number<float>(Field(record, "x")),
                              Number<float>(Field(record, "y")), Number<float>(Field(record, "width")),
                              Number<float>(Field(record, "height"))};
            maximum = std::max(maximum, id);
            if (!graph.layout_.frames.emplace(id, std::move(frame)).second)
            {
                throw ArchiveError("Duplicate frame ID");
            }
        }
        const auto view = Field(layout, "view");
        Fields(view, {"saved", "centerX", "centerY", "zoom"});
        graph.layout_.view = {Boolean(Field(view, "saved")), Number<float>(Field(view, "centerX")),
                              Number<float>(Field(view, "centerY")), Number<float>(Field(view, "zoom"))};
        if (!graph.nextId_ || graph.nextId_ <= maximum || graph.nextId_ == std::numeric_limits<Id>::max())
        {
            throw ArchiveError("Invalid graph ID allocator");
        }
        const auto existing = asset.scopes.find(scope);
        if (existing != asset.scopes.end() && existing->second != metadata)
        {
            throw ArchiveError("Shared group metadata differs between copies");
        }
        asset.scopes[scope] = std::move(metadata);
        return graph;
    }

    LXMaterialAsset Read(const std::string& text)
    {
        auto tree = ParseTree(text);
        const auto root = tree.crootref();
        Fields(root, {"kind", "schemaVersion", "domain", "graphId", "activeOutput", "blackboard", "graph"});
        version = Number<unsigned>(Field(root, "schemaVersion"));
        if (version > LXMaterialArchive::SchemaVersion)
        {
            throw CompatibilityError("Unsupported shadergraph schema version");
        }
        if (String(Field(root, "kind")) != "LatticeMaterial" || String(Field(root, "domain")) != "material")
        {
            throw CompatibilityError("Expected LatticeMaterial in the material domain");
        }
        asset.graphId = Number<Id>(Field(root, "graphId"));
        asset.activeOutput = Number<Id>(Field(root, "activeOutput"));
        for (auto record : Sequence(Field(root, "blackboard")).children())
        {
            Fields(record, {"id", "identifier", "name", "type", "default", "colorSpace", "exposed"});
            LXMaterialParameter parameter;
            parameter.id = Number<Id>(Field(record, "id"));
            parameter.identifier = String(Field(record, "identifier"));
            parameter.name = String(Field(record, "name"));
            parameter.type = ReadType(Field(record, "type"));
            parameter.value = ReadValue(Field(record, "default"));
            parameter.colorSpace = version == 0
                                       ? (parameter.type == PinType::Color ? LXColorSpace::Linear : LXColorSpace::Data)
                                       : ReadSpace(Field(record, "colorSpace"));
            parameter.exposed = Boolean(Field(record, "exposed"));
            asset.blackboard.push_back(std::move(parameter));
        }
        asset.graph = Graph(Field(root, "graph"), 0);
        const auto issues = asset.Validate();
        for (const Issue& issue : issues)
        {
            if (issue.severity == Issue::Severity::Error)
            {
                if (issue.code == "material_node_revision" || issue.code == "node_schema" ||
                    issue.code == "material_property")
                {
                    throw CompatibilityError(issue.code + ": " + issue.message);
                }
                throw ArchiveError(issue.code + ": " + issue.message);
            }
        }
        return std::move(asset);
    }
};

LXMaterialAsset::LXMaterialAsset(LXMaterialDefinitions definitions)
    : graph("material", definitions.nodes), definitions_(std::move(definitions))
{
}

Id LXMaterialAsset::CreateNode(const std::string& type, float x, float y, std::string title)
{
    const auto* definition = definitions_.nodes->Find(type);
    if (!definition)
    {
        return 0;
    }
    auto spec = definition->defaults;
    if (!title.empty())
    {
        spec.title = std::move(title);
    }
    const Id id = graph.AddNode(spec, x, y);
    if (id)
    {
        const Node& node = *graph.FindNode(id);
        const auto schema = definitions_.schemas.find(type);
        scopes[0].nodes[id] = {schema == definitions_.schemas.end() ? 1u : schema->second.revision};
        for (const Pin& pin : node.pins)
        {
            scopes[0].sockets[pin.id] = SocketState(0, pin);
        }
    }
    return id;
}

Id LXMaterialAsset::CollapseToGroup(const std::vector<Id>& nodes, std::string name)
{
    const LXMaterialAsset before = *this;
    const LXGraph previous = graph;
    std::map<Id, LXMaterialSocketState> socketStates;
    for (Id id : nodes)
    {
        if (const Node* node = graph.FindNode(id))
        {
            for (const Pin& pin : node->pins)
            {
                socketStates.emplace(pin.id, SocketState(0, pin));
            }
        }
    }
    LXGroupCollapseMapping mapping;
    const Id instance = graph.CollapseToGroup(nodes, std::move(name), 0, &mapping);
    if (!instance)
    {
        return 0;
    }
    const Node& node = *graph.FindNode(instance);
    const auto& group = *graph.FindGroup(node.groupId);
    for (const auto& [oldId, newId] : mapping.nodes)
    {
        scopes[group.id].nodes[newId] = NodeState(0, *previous.FindNode(oldId));
        scopes[0].nodes.erase(oldId);
    }
    for (const auto& [oldId, newId] : mapping.pins)
    {
        const auto state = socketStates.at(oldId);
        scopes[group.id].sockets[newId] = state;
        scopes[0].sockets.erase(oldId);
    }
    for (std::size_t index = 0; index < group.sockets.size(); ++index)
    {
        const auto& socket = group.sockets[index];
        for (const Link& link : group.body->Links())
        {
            const Id connected = link.output == socket.internalPin  ? link.input
                                 : link.input == socket.internalPin ? link.output
                                                                    : 0;
            if (connected && scopes[group.id].sockets.contains(connected))
            {
                const auto state = scopes[group.id].sockets.at(connected);
                scopes[group.id].sockets[socket.internalPin] = state;
                scopes[0].sockets[node.pins[index].id] = state;
                break;
            }
        }
    }
    // Core exposes every unlinked input. Material's disabled sockets and
    // implicit geometry/UV inputs must retain their defaults inside the group.
    LXGraph body(*group.body);
    auto sockets = group.sockets;
    std::set<Id> remove;
    for (std::size_t index = 0; index < group.sockets.size(); ++index)
    {
        const auto& socket = group.sockets[index];
        const auto state = SocketState(group.id, *body.FindPin(socket.internalPin));
        if (socket.direction != Direction::Input || (state.enabled && !state.hideValue))
            continue;
        bool explicitConnection = false;
        for (const auto& link : body.Links())
        {
            if (link.output != socket.internalPin)
                continue;
            for (const auto& [oldId, newId] : mapping.pins)
                if (newId == link.input)
                    explicitConnection |= std::ranges::any_of(
                        previous.Links(), [&](const auto& original) { return original.input == oldId; });
        }
        if (explicitConnection)
            continue;
        remove.insert(socket.id);
        body.RemoveGroupBoundaryPin(socket.internalPin);
        scopes[group.id].sockets.erase(socket.internalPin);
        scopes[0].sockets.erase(node.pins[index].id);
    }
    if (!remove.empty())
    {
        std::erase_if(sockets, [&](const auto& socket) { return remove.contains(socket.id); });
        if (!graph.UpdateGroup(group.id, group.name, body, std::move(sockets)))
        {
            *this = before;
            return 0;
        }
    }
    return instance;
}

LXMaterialSocketState LXMaterialAsset::SocketState(Id scope, const Pin& pin) const
{
    const auto metadata = scopes.find(scope);
    if (metadata != scopes.end())
    {
        const auto state = metadata->second.sockets.find(pin.id);
        if (state != metadata->second.sockets.end())
        {
            return state->second;
        }
    }
    const LXGraph* owner = &graph;
    std::map<Id, const LXGraph*> graphs{{0, &graph}};
    CollectGraphs(graph, graphs);
    if (graphs.contains(scope))
    {
        owner = graphs.at(scope);
    }
    const Node* node = owner->FindNode(pin.node);
    if (node)
    {
        const auto schema = definitions_.schemas.find(node->type);
        if (schema != definitions_.schemas.end())
        {
            for (std::size_t index = 0; index < node->pins.size() && index < schema->second.sockets.size(); ++index)
            {
                if (node->pins[index].id == pin.id)
                {
                    return schema->second.sockets[index];
                }
            }
        }
    }
    return {true, false, false, pin.type == PinType::Color ? LXColorSpace::Linear : LXColorSpace::Data};
}

LXMaterialNodeState LXMaterialAsset::NodeState(Id scope, const Node& node) const
{
    const auto metadata = scopes.find(scope);
    if (metadata != scopes.end() && metadata->second.nodes.contains(node.id))
    {
        return metadata->second.nodes.at(node.id);
    }
    const auto schema = definitions_.schemas.find(node.type);
    return {schema == definitions_.schemas.end() ? 1u : schema->second.revision};
}

std::vector<Issue> LXMaterialAsset::Validate() const
{
    auto issues = graph.Validate();
    if (!definitions_.nodes)
    {
        issues.push_back({"material_domain", "Missing Material definition registry"});
        return issues;
    }
    if (!graphId || graph.Domain() != "material" || graph.Definitions() != definitions_.nodes || !definitions_.nodes)
    {
        issues.push_back(
            {"material_domain", "Material asset requires its Material definition registry and graph identity"});
    }
    if (activeOutput &&
        (!graph.FindNode(activeOutput) || graph.FindNode(activeOutput)->type != "ShaderNodeOutputMaterial"))
    {
        issues.push_back({"material_output", "Active output is not a Material Output node", activeOutput});
    }
    std::set<Id> parameterIds;
    std::set<std::string> parameterNames;
    for (const auto& parameter : blackboard)
    {
        if (!parameter.id || !parameterIds.insert(parameter.id).second || parameter.identifier.empty() ||
            parameter.name.empty() || !parameterNames.insert(parameter.identifier).second ||
            !definitions_.nodes->AllowsPin("material", parameter.type) || parameter.type == PinType::Closure ||
            parameter.type == PinType::Surface || !IsSocketValueValid(parameter.type, parameter.value) ||
            !ValidSpace(parameter.type, parameter.colorSpace))
        {
            issues.push_back({"material_blackboard", "Invalid parameter identity, type, default or color space"});
        }
    }
    std::map<Id, const LXGraph*> graphs{{0, &graph}};
    CollectGraphs(graph, graphs);
    for (const auto& [scope, owner] : graphs)
    {
        const auto firstIssue = issues.size();
        if (owner->Domain() != "material" || owner->Definitions() != definitions_.nodes)
        {
            issues.push_back({"material_domain", "Nested group belongs to a different domain or registry"});
        }
        for (const Node& node : owner->Nodes())
        {
            const auto state = NodeState(scope, node);
            const auto schema = definitions_.schemas.find(node.type);
            if (!state.schemaRevision ||
                (schema != definitions_.schemas.end() && state.schemaRevision != schema->second.revision))
            {
                issues.push_back(
                    {"material_node_revision", "Definition revision requires an explicit migration", node.id});
            }
            if (schema != definitions_.schemas.end())
            {
                for (const auto& [key, allowed] : schema->second.enumProperties)
                {
                    const auto property = node.properties.find(key);
                    // Archives written before socket-backed model parameters
                    // omit this optional property and use the Blackboard default.
                    if (property == node.properties.end() && key == "valueSource" &&
                        node.type.starts_with("LXParameter"))
                    {
                        continue;
                    }
                    if (property == node.properties.end() || !allowed.contains(property->second))
                    {
                        issues.push_back({"material_property", "Unsupported enum value for " + key, node.id});
                    }
                }
            }
            for (const Pin& pin : node.pins)
            {
                if (!ValidSpace(pin.type, SocketState(scope, pin).colorSpace))
                {
                    issues.push_back(
                        {"material_color_space", "Color-space intent is invalid for this socket", node.id, pin.id});
                }
            }
        }
        for (std::size_t index = firstIssue; index < issues.size(); ++index)
        {
            issues[index].scope = scope;
        }
    }
    return issues;
}

bool LXMaterialAsset::Equals(const LXMaterialAsset& other) const
{
    // Canonical archive compares effective metadata too: newly created default metadata may be implicit.
    return graphId == other.graphId && activeOutput == other.activeOutput && blackboard == other.blackboard &&
           graph.Equals(other.graph) && LXMaterialArchive::Write(*this) == LXMaterialArchive::Write(other);
}

std::string LXMaterialArchive::Write(const LXMaterialAsset& asset)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::boolalpha << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << "{\n  \"kind\":\"LatticeMaterial\",\n  \"schemaVersion\":" << SchemaVersion
        << ",\n  \"domain\":\"material\",\n  \"graphId\":" << asset.graphId
        << ",\n  \"activeOutput\":" << asset.activeOutput << ",\n  \"blackboard\":[";
    bool first = true;
    for (const auto& parameter : asset.blackboard)
    {
        out << (first ? "\n" : ",\n") << "    {\"id\":" << parameter.id
            << ",\"identifier\":" << Quote(parameter.identifier) << ",\"name\":" << Quote(parameter.name)
            << ",\"type\":" << Quote(PinTypeName(parameter.type)) << ",\"default\":";
        first = false;
        WriteValue(out, parameter.value);
        out << ",\"colorSpace\":" << Quote(SpaceName(parameter.colorSpace)) << ",\"exposed\":" << parameter.exposed
            << '}';
    }
    out << "\n  ],\n  \"graph\":";
    WriteGraph(out, asset, asset.graph, 0, 1);
    out << "\n}\n";
    return out.str();
}

std::optional<LXMaterialAsset> LXMaterialArchive::Read(const std::string& text,
                                                       const LXMaterialDefinitions& definitions, std::string* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        return Reader(definitions).Read(text);
    }
    catch (const std::exception& exception)
    {
        Fail(error, exception.what());
        return std::nullopt;
    }
}

std::optional<LXMaterialAsset> LXMaterialAsset::Load(const std::filesystem::path& path,
                                                     const LXMaterialDefinitions& definitions, std::string* error)
{
    if (error)
    {
        error->clear();
    }
    try
    {
        return LXMaterialArchive::Reader(definitions).Read(ReadFile(path));
    }
    catch (const CompatibilityError& exception)
    {
        Fail(error, exception.what());
        return std::nullopt;
    }
    catch (const std::exception& exception)
    {
        const std::string primaryError = exception.what();
        try
        {
            auto backup = path;
            backup += L".bak";
            auto result = LXMaterialArchive::Reader(definitions).Read(ReadFile(backup));
            Fail(error, "Recovered backup after: " + primaryError);
            return result;
        }
        catch (const std::exception&)
        {
            Fail(error, primaryError);
            return std::nullopt;
        }
    }
}

bool LXMaterialAsset::Save(const std::filesystem::path& path, std::string* error) const
{
    if (error)
    {
        error->clear();
    }
    const auto issues = Validate();
    if (HasErrors(issues))
    {
        for (const Issue& issue : issues)
        {
            if (issue.severity == Issue::Severity::Error)
            {
                return Fail(error, issue.code + ": " + issue.message);
            }
        }
    }
    const auto suffix =
        L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(saveSequence.fetch_add(1));
    auto staged = path;
    staged += suffix;
    auto stagedBackup = staged;
    stagedBackup += L".bak";
    try
    {
        const auto text = LXMaterialArchive::Write(*this);
        std::ofstream out(staged, std::ios::binary | std::ios::trunc);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out.good())
        {
            throw ArchiveError("Cannot write staged shadergraph");
        }
        out.close();
        const auto verified = LXMaterialArchive::Reader(definitions_).Read(ReadFile(staged));
        if (!Equals(verified))
        {
            throw ArchiveError("Exact shadergraph round-trip verification failed");
        }
        if (std::filesystem::exists(path))
        {
            bool validPrimary = false;
            try
            {
                LXMaterialArchive::Reader(definitions_).Read(ReadFile(path));
                validPrimary = true;
            }
            catch (const CompatibilityError&)
            {
                throw;
            }
            catch (const ArchiveError&)
            {
                // Leave a valid backup intact when repairing a truncated primary.
            }
            if (validPrimary)
            {
                std::filesystem::copy_file(path, stagedBackup);
                auto backup = path;
                backup += L".bak";
                if (!MoveFileExW(stagedBackup.c_str(), backup.c_str(),
                                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                {
                    throw ArchiveError("Cannot replace shadergraph backup");
                }
            }
        }
        if (!MoveFileExW(staged.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            throw ArchiveError("Cannot atomically replace shadergraph");
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        std::error_code ignored;
        std::filesystem::remove(staged, ignored);
        std::filesystem::remove(stagedBackup, ignored);
        return Fail(error, exception.what());
    }
}

} // namespace LX
