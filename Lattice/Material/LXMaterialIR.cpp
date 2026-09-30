#include "LXMaterialIR.h"

#include <algorithm>
#include <charconv>
#include <set>

namespace LX
{
namespace
{
void CollectScopes(const LXGraph& graph, std::map<Id, const LXGroupDefinition*>& scopes)
{
    for (const auto& [id, group] : graph.Groups())
    {
        if (scopes.emplace(id, &group).second)
        {
            CollectScopes(*group.body, scopes);
        }
    }
}

bool IsBoundary(const Node& node)
{
    return node.type == "LX_GROUP_INPUT" || node.type == "LX_GROUP_OUTPUT";
}

bool LowerScope(const LXMaterialAsset& asset, Id scopeId, const LXGraph& graph, LXMaterialIRScope& output,
                std::vector<Issue>& issues)
{
    output.groupId = scopeId;
    const auto firstIssue = issues.size();
    std::map<Id, std::size_t> dependencies;
    std::map<Id, std::vector<Id>> consumers;
    std::set<Id> ready;
    for (const Node& node : graph.Nodes())
    {
        dependencies[node.id] = 0;
        const auto state = asset.NodeState(scopeId, node);
        const auto definition = asset.Definitions().schemas.find(node.type);
        if (!node.groupId && !IsBoundary(node) && definition == asset.Definitions().schemas.end())
        {
            issues.push_back(
                {"material_unknown_node", "Unknown definition in scope " + std::to_string(scopeId), node.id});
        }
        else if (definition != asset.Definitions().schemas.end() && state.schemaRevision != definition->second.revision)
        {
            issues.push_back(
                {"material_node_revision", "Node schema revision requires an explicit migration", node.id});
        }
        if (node.type.starts_with("LXParameter"))
        {
            Id parameterId = 0;
            const auto property = node.properties.find("parameter");
            bool validId = property != node.properties.end();
            if (validId)
            {
                const auto& value = property->second;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), parameterId);
                validId = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
            }
            const auto parameter = std::find_if(asset.blackboard.begin(), asset.blackboard.end(),
                                                [&](const auto& item) { return item.id == parameterId; });
            if (!validId || parameter == asset.blackboard.end() || node.pins.size() != 1 ||
                parameter->type != node.pins.front().type)
            {
                issues.push_back({"material_parameter", "Parameter ID is missing or its socket type differs", node.id});
            }
        }
    }
    for (const Link& link : graph.Links())
    {
        const Pin& source = *graph.FindPin(link.output);
        const Pin& target = *graph.FindPin(link.input);
        if (!asset.SocketState(scopeId, source).enabled || !asset.SocketState(scopeId, target).enabled)
        {
            issues.push_back(
                {"material_inactive_socket", "A connection uses an inactive socket", target.node, target.id});
        }
        ++dependencies[target.node];
        consumers[source.node].push_back(target.node);
        output.links.push_back({link.id, link.output, link.input, source.type});
    }
    for (const auto& [id, count] : dependencies)
    {
        if (!count)
        {
            ready.insert(id);
        }
    }
    while (!ready.empty())
    {
        const Id id = *ready.begin();
        ready.erase(ready.begin());
        const Node& node = *graph.FindNode(id);
        LXMaterialIRNode lowered;
        lowered.id = node.id;
        lowered.groupId = node.groupId;
        lowered.definition = node.type;
        lowered.schemaRevision = asset.NodeState(scopeId, node).schemaRevision;
        lowered.properties = node.properties;
        for (const Pin& pin : node.pins)
        {
            lowered.sockets.push_back({pin.id, pin.interfaceId, pin.Identifier(), pin.direction, pin.type, pin.multiple,
                                       pin.value, asset.SocketState(scopeId, pin)});
        }
        output.nodes.push_back(std::move(lowered));
        for (Id consumer : consumers[id])
        {
            if (--dependencies[consumer] == 0)
            {
                ready.insert(consumer);
            }
        }
    }
    std::ranges::sort(output.links, {}, &LXMaterialIRLink::id);
    if (output.nodes.size() != graph.Nodes().size())
    {
        issues.push_back({"material_cycle", "Material data graph must be acyclic"});
    }
    for (std::size_t index = firstIssue; index < issues.size(); ++index)
    {
        issues[index].scope = scopeId;
    }
    return issues.empty();
}
} // namespace

std::optional<LXMaterialIR> BuildMaterialIR(const LXMaterialAsset& asset, std::vector<Issue>* resultIssues)
{
    std::vector<Issue> issues = asset.Validate();
    std::erase_if(issues, [](const Issue& issue) { return issue.severity == Issue::Severity::Warning; });
    const Node* output = asset.graph.FindNode(asset.activeOutput);
    if (!output || output->type != "ShaderNodeOutputMaterial")
    {
        issues.push_back({"material_output", "Select a Material Output node", asset.activeOutput});
    }
    LXMaterialIR result;
    result.graphId = asset.graphId;
    result.activeOutput = asset.activeOutput;
    result.blackboard = asset.blackboard;
    std::map<Id, const LXGroupDefinition*> groups;
    CollectScopes(asset.graph, groups);
    std::map<Id, LXSocketValue> socketDefaults;
    const auto defaults = [&](const LXGraph& graph) {
        for (const auto& node : graph.Nodes())
        {
            const auto source = node.properties.find("valueSource");
            if (!node.type.starts_with("LXParameter") || source == node.properties.end() ||
                source->second != "socket" || node.pins.size() != 1)
                continue;
            Id id{};
            const auto binding = node.properties.find("parameter");
            if (binding == node.properties.end())
            {
                issues.push_back({"material_parameter", "Socket parameter has no Blackboard binding", node.id});
                continue;
            }
            const auto& text = binding->second;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
            const auto parameter = std::ranges::find(result.blackboard, id, &LXMaterialParameter::id);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                parameter == result.blackboard.end() || std::holds_alternative<std::monostate>(node.pins.front().value))
            {
                issues.push_back(
                    {"material_parameter", "Socket parameter requires a declared binding and typed default", node.id});
                continue;
            }
            const auto [existing, inserted] = socketDefaults.emplace(id, node.pins.front().value);
            if (!inserted && existing->second != node.pins.front().value)
            {
                issues.push_back({"material_parameter", "Socket parameter has conflicting default sources", node.id});
                continue;
            }
            parameter->value = node.pins.front().value;
        }
    };
    defaults(asset.graph);
    for (const auto& [id, group] : groups)
    {
        defaults(*group->body);
    }
    std::ranges::sort(result.blackboard, {}, &LXMaterialParameter::id);
    if (issues.empty())
    {
        LXMaterialIRScope root;
        LowerScope(asset, 0, asset.graph, root, issues);
        result.scopes.push_back(std::move(root));
        for (const auto& [id, group] : groups)
        {
            LXMaterialIRScope scope;
            scope.groupInterface = group->sockets;
            LowerScope(asset, id, *group->body, scope, issues);
            result.scopes.push_back(std::move(scope));
        }
    }
    if (resultIssues)
    {
        *resultIssues = issues;
    }
    return issues.empty() ? std::optional(std::move(result)) : std::nullopt;
}

LXMaterialDocument::LXMaterialDocument(LXMaterialAsset asset) : asset_(std::move(asset)) {}

bool LXMaterialDocument::PublishIR(std::vector<Issue>* issues)
{
    auto candidate = BuildMaterialIR(asset_, issues);
    if (!candidate)
    {
        return false;
    }
    if (!lastValidIR_ || *candidate != *lastValidIR_)
    {
        lastValidIR_ = std::move(candidate);
        ++generation_;
    }
    return true;
}

bool LXMaterialDocument::Reload(const std::filesystem::path& path, std::string* error)
{
    auto candidate = LXMaterialAsset::Load(path, asset_.Definitions(), error);
    if (!candidate)
    {
        return false;
    }
    asset_ = std::move(*candidate);
    undo_.clear();
    redo_.clear();
    // Authoring can contain unknown nodes; failed lowering never replaces the last valid IR generation.
    PublishIR();
    return true;
}

bool LXMaterialDocument::Edit(const std::function<bool(LXMaterialAsset&)>& operation)
{
    LXMaterialAsset candidate = asset_;
    if (!operation(candidate))
    {
        return false;
    }
    const auto issues = candidate.Validate();
    if (std::ranges::any_of(issues, [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return false;
    }
    if (candidate.Equals(asset_))
    {
        return false;
    }
    if (undo_.size() == 128)
    {
        undo_.erase(undo_.begin());
    }
    undo_.push_back(asset_);
    asset_ = std::move(candidate);
    redo_.clear();
    PublishIR();
    return true;
}

bool LXMaterialDocument::Undo()
{
    if (undo_.empty())
    {
        return false;
    }
    redo_.push_back(asset_);
    asset_ = std::move(undo_.back());
    undo_.pop_back();
    PublishIR();
    return true;
}

bool LXMaterialDocument::Redo()
{
    if (redo_.empty())
    {
        return false;
    }
    undo_.push_back(asset_);
    asset_ = std::move(redo_.back());
    redo_.pop_back();
    PublishIR();
    return true;
}

} // namespace LX
