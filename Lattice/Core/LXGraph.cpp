#include "LXGraph.h"
#include "LXNodeDefinition.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <type_traits>
#include <Windows.h>

namespace LX
{
namespace
{
constexpr std::size_t kUndoLimit = 128;
constexpr std::size_t kDocumentLimit = 100000;
constexpr unsigned kMaxGroupNesting = 16;
std::atomic_uint64_t gSaveSequence = 0;
bool IsAcyclicGroupTree(const LXGraph& graph, unsigned depth, std::unordered_set<const LXGraph*>& active)
{
    if (depth > kMaxGroupNesting || !active.insert(&graph).second)
    {
        return false;
    }
    for (const auto& [id, group] : graph.Groups())
    {
        if (!group.body || !IsAcyclicGroupTree(*group.body, depth + 1, active))
        {
            active.erase(&graph);
            return false;
        }
    }
    active.erase(&graph);
    return true;
}

Id MaxNextIdInTree(const LXGraph& graph)
{
    Id maximum = graph.NextId();
    for (const auto& [id, group] : graph.Groups())
    {
        if (group.body)
        {
            maximum = std::max(maximum, MaxNextIdInTree(*group.body));
        }
    }
    return maximum;
}

struct CanonicalGroup
{
    const LXGroupDefinition* definition = nullptr;
    unsigned depth = 0;
};

void CollectCanonicalGroups(const LXGraph& graph, unsigned depth, std::map<Id, CanonicalGroup>& groups)
{
    for (const auto& [id, group] : graph.Groups())
    {
        const auto found = groups.find(id);
        if (found == groups.end() || depth < found->second.depth)
        {
            groups[id] = {&group, depth};
        }
        CollectCanonicalGroups(*group.body, depth + 1, groups);
    }
}

enum class GroupReferenceIssue
{
    None,
    Cycle,
    Depth,
    Missing
};

GroupReferenceIssue VisitGroupReference(Id id, const std::map<Id, CanonicalGroup>& groups,
                                        std::map<Id, unsigned>& state, unsigned depth, std::vector<Id>* order)
{
    if (depth > kMaxGroupNesting)
    {
        return GroupReferenceIssue::Depth;
    }
    const auto found = groups.find(id);
    if (found == groups.end())
    {
        return GroupReferenceIssue::Missing;
    }
    if (state[id] == 1)
    {
        return GroupReferenceIssue::Cycle;
    }
    if (state[id] == 2)
    {
        return GroupReferenceIssue::None;
    }
    state[id] = 1;
    for (const Node& node : found->second.definition->body->Nodes())
    {
        if (!node.groupId)
        {
            continue;
        }
        const GroupReferenceIssue issue = VisitGroupReference(node.groupId, groups, state, depth + 1, order);
        if (issue != GroupReferenceIssue::None)
        {
            return issue;
        }
    }
    state[id] = 2;
    if (order)
    {
        order->push_back(id);
    }
    return GroupReferenceIssue::None;
}

GroupReferenceIssue CheckGroupReferences(const LXGraph& graph, std::vector<Id>* order = nullptr)
{
    std::map<Id, CanonicalGroup> groups;
    CollectCanonicalGroups(graph, 0, groups);
    std::map<Id, unsigned> state;
    for (const auto& [id, group] : groups)
    {
        const GroupReferenceIssue issue = VisitGroupReference(id, groups, state, 1, order);
        if (issue != GroupReferenceIssue::None)
        {
            return issue;
        }
    }
    return GroupReferenceIssue::None;
}

bool SharedGroupCopiesMatch(const LXGraph& graph, const std::map<Id, CanonicalGroup>& canonical)
{
    for (const auto& [id, group] : graph.Groups())
    {
        const LXGroupDefinition& source = *canonical.at(id).definition;
        if (group.name != source.name || group.sockets != source.sockets || !group.body->Equals(*source.body) ||
            !SharedGroupCopiesMatch(*group.body, canonical))
        {
            return false;
        }
    }
    return true;
}

bool IsGroupBoundaryType(const std::string& type)
{
    return type == "LX_GROUP_INPUT" || type == "LX_GROUP_OUTPUT";
}

Direction GroupBoundaryPinDirection(const std::string& type)
{
    return type == "LX_GROUP_INPUT" ? Direction::Output : Direction::Input;
}

std::filesystem::path Utf8Path(const std::string& value)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
bool Fail(std::string* error, const std::string& value)
{
    if (error)
    {
        *error = value;
    }
    return false;
}

void WriteSocketValue(std::ostream& out, const LXSocketValue& value)
{
    // Wire tags are an archive contract, independent of future variant storage.
    const unsigned tag = std::visit([](const auto& field) -> unsigned {
        using T = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<T, bool>)
        {
            return 1;
        }
        else if constexpr (std::is_same_v<T, std::int64_t>)
        {
            return 2;
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            return 3;
        }
        else if constexpr (std::is_same_v<T, std::array<double, 3>>)
        {
            return 4;
        }
        else if constexpr (std::is_same_v<T, std::array<double, 4>>)
        {
            return 5;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return 6;
        }
        else if constexpr (std::is_same_v<T, std::array<double, 2>>)
        {
            return 7;
        }
        else
        {
            return 0;
        }
    }, value);
    out << ' ' << tag << std::setprecision(std::numeric_limits<double>::max_digits10);
    switch (tag)
    {
    case 1:
        out << ' ' << std::get<bool>(value);
        break;
    case 2:
        out << ' ' << std::get<std::int64_t>(value);
        break;
    case 3:
        out << ' ' << std::get<double>(value);
        break;
    case 4:
        for (double component : std::get<std::array<double, 3>>(value))
        {
            out << ' ' << component;
        }
        break;
    case 5:
        for (double component : std::get<std::array<double, 4>>(value))
        {
            out << ' ' << component;
        }
        break;
    case 6:
        out << ' ' << std::quoted(std::get<std::string>(value));
        break;
    case 7:
        for (double component : std::get<std::array<double, 2>>(value))
        {
            out << ' ' << component;
        }
        break;
    default:
        break;
    }
}

bool ReadSocketValue(std::istream& in, LXSocketValue& value)
{
    int kind = 0;
    if (!(in >> kind))
    {
        return false;
    }
    switch (kind)
    {
    case 0:
        value = std::monostate{};
        return true;
    case 1: {
        int field = 0;
        if (!(in >> field) || (field != 0 && field != 1))
        {
            return false;
        }
        value = field != 0;
        return true;
    }
    case 2: {
        std::int64_t field = 0;
        if (!(in >> field))
        {
            return false;
        }
        value = field;
        return true;
    }
    case 3: {
        double field = 0;
        if (!(in >> field))
        {
            return false;
        }
        value = field;
        return true;
    }
    case 4: {
        std::array<double, 3> field{};
        if (!(in >> field[0] >> field[1] >> field[2]))
        {
            return false;
        }
        value = field;
        return true;
    }
    case 5: {
        std::array<double, 4> field{};
        if (!(in >> field[0] >> field[1] >> field[2] >> field[3]))
        {
            return false;
        }
        value = field;
        return true;
    }
    case 6: {
        std::string field;
        if (!(in >> std::quoted(field)))
        {
            return false;
        }
        value = std::move(field);
        return true;
    }
    case 7: {
        std::array<double, 2> field{};
        if (!(in >> field[0] >> field[1]))
        {
            return false;
        }
        value = field;
        return true;
    }
    default:
        return false;
    }
}
} // namespace

bool IsSocketValueValid(PinType type, const LXSocketValue& value)
{
    if (std::holds_alternative<std::monostate>(value))
    {
        return true;
    }
    switch (type)
    {
    case PinType::Bool:
        return std::holds_alternative<bool>(value);
    case PinType::Int:
        return std::holds_alternative<std::int64_t>(value);
    case PinType::Float:
        return std::holds_alternative<double>(value) && std::isfinite(std::get<double>(value));
    case PinType::Vector2:
        if (const auto* components = std::get_if<std::array<double, 2>>(&value))
        {
            return std::all_of(components->begin(), components->end(),
                               [](double component) { return std::isfinite(component); });
        }
        return false;
    case PinType::Vector:
    case PinType::Normal:
        if (const auto* components = std::get_if<std::array<double, 3>>(&value))
        {
            return std::all_of(components->begin(), components->end(),
                               [](double component) { return std::isfinite(component); });
        }
        return false;
    case PinType::Color:
        if (const auto* components = std::get_if<std::array<double, 4>>(&value))
        {
            return std::all_of(components->begin(), components->end(),
                               [](double component) { return std::isfinite(component); });
        }
        return false;
    case PinType::Texture:
    case PinType::Sampler:
        return std::holds_alternative<std::string>(value);
    case PinType::Flow:
    case PinType::Surface:
    case PinType::Closure:
        return false;
    }
    return false;
}

LXGraph::LXGraph(std::string domain, std::shared_ptr<const LXNodeDefinitionRegistry> definitions)
    : domain_(std::move(domain)), definitions_(std::move(definitions))
{
}

bool LXGraph::AllowsPin(PinType type) const
{
    if (definitions_)
    {
        return definitions_->AllowsPin(domain_, type);
    }
    return (domain_ != "material" || type != PinType::Flow) &&
           (domain_ == "material" || (type != PinType::Surface && type != PinType::Sampler && type != PinType::Closure));
}

bool LXGraph::CanEditNode(const Node& node) const
{
    if (node.groupId || node.type == "LX_GROUP")
    {
        return MatchesGroupInstance(node);
    }
    if (IsGroupBoundaryType(node.type))
    {
        return !node.groupId && !node.dynamicPins && node.properties.empty() &&
               std::all_of(node.pins.begin(), node.pins.end(), [&](const Pin& pin) {
                   return pin.direction == GroupBoundaryPinDirection(node.type) && !pin.multiple && !pin.dynamic &&
                          !pin.interfaceId && AllowsPin(pin.type) && IsSocketValueValid(pin.type, pin.value);
               });
    }
    return !definitions_ || definitions_->MatchesNode(domain_, node);
}

const LXGroupDefinition* LXGraph::FindGroup(Id id) const
{
    const auto found = groups_.find(id);
    return found == groups_.end() ? nullptr : &found->second;
}

bool LXGraph::MatchesGroupDefinition(const LXGroupDefinition& definition) const
{
    if (!definition.id || definition.name.empty() || !definition.body || definition.body->Domain() != domain_ ||
        definition.sockets.empty())
    {
        return false;
    }
    std::unordered_set<const LXGraph*> active;
    if (!IsAcyclicGroupTree(*definition.body, 1, active))
    {
        return false;
    }
    const Node* inputBoundary = nullptr;
    const Node* outputBoundary = nullptr;
    for (const Node& node : definition.body->Nodes())
    {
        if (node.type == "LX_GROUP_INPUT")
        {
            if (inputBoundary)
            {
                return false;
            }
            inputBoundary = &node;
        }
        else if (node.type == "LX_GROUP_OUTPUT")
        {
            if (outputBoundary)
            {
                return false;
            }
            outputBoundary = &node;
        }
    }
    const bool boundaryMode = inputBoundary || outputBoundary;
    if (boundaryMode && (!inputBoundary || !outputBoundary))
    {
        return false;
    }
    const auto bodyIssues = definition.body->Validate();
    if (std::any_of(bodyIssues.begin(), bodyIssues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return false;
    }
    std::unordered_set<Id> socketIds;
    std::unordered_set<Id> internalPins;
    std::set<std::pair<Direction, std::string>> identifiers;
    bool hasOutput = false;
    for (const LXGroupSocket& socket : definition.sockets)
    {
        const Pin* internal = definition.body->FindPin(socket.internalPin);
        if (!socket.id || socket.identifier.empty() || socket.name.empty() || !internal ||
            internal->type != socket.type || !AllowsPin(socket.type) ||
            !IsSocketValueValid(socket.type, socket.value) || !socketIds.insert(socket.id).second ||
            !internalPins.insert(socket.internalPin).second ||
            !identifiers.emplace(socket.direction, socket.identifier).second)
        {
            return false;
        }
        if (boundaryMode)
        {
            const Node* boundary = socket.direction == Direction::Input ? inputBoundary : outputBoundary;
            if (internal->node != boundary->id || internal->direction == socket.direction ||
                internal->name != socket.name || internal->Identifier() != socket.identifier ||
                internal->value != socket.value)
            {
                return false;
            }
        }
        else if (internal->direction != socket.direction ||
                 (socket.direction == Direction::Input &&
                  std::any_of(definition.body->Links().begin(), definition.body->Links().end(),
                              [&](const Link& link) { return link.input == socket.internalPin; })))
        {
            return false;
        }
        hasOutput |= socket.direction == Direction::Output;
    }
    return hasOutput &&
           (!boundaryMode || internalPins.size() == inputBoundary->pins.size() + outputBoundary->pins.size());
}

bool LXGraph::SynchronizeGroupBoundaryPins(LXGraph& body, const std::vector<LXGroupSocket>& sockets) const
{
    const bool boundaryMode = std::any_of(body.nodes_.begin(), body.nodes_.end(),
                                          [](const Node& node) { return IsGroupBoundaryType(node.type); });
    if (!boundaryMode)
    {
        return true;
    }
    for (const LXGroupSocket& socket : sockets)
    {
        const char* expectedType = socket.direction == Direction::Input ? "LX_GROUP_INPUT" : "LX_GROUP_OUTPUT";
        const Direction expectedDirection = socket.direction == Direction::Input ? Direction::Output : Direction::Input;
        bool found = false;
        for (Node& node : body.nodes_)
        {
            for (Pin& pin : node.pins)
            {
                if (pin.id != socket.internalPin)
                {
                    continue;
                }
                if (node.type != expectedType || pin.direction != expectedDirection || pin.type != socket.type)
                {
                    return false;
                }
                pin.name = socket.name;
                pin.identifier = socket.identifier;
                pin.value = socket.value;
                found = true;
            }
        }
        if (!found)
        {
            return false;
        }
    }
    for (Node& node : body.nodes_)
    {
        if (!IsGroupBoundaryType(node.type))
        {
            continue;
        }
        std::vector<Pin> ordered;
        ordered.reserve(node.pins.size());
        for (const LXGroupSocket& socket : sockets)
        {
            const auto pin = std::find_if(node.pins.begin(), node.pins.end(),
                                          [&](const Pin& candidate) { return candidate.id == socket.internalPin; });
            if (pin != node.pins.end())
            {
                ordered.push_back(*pin);
            }
        }
        if (ordered.size() != node.pins.size())
        {
            return false;
        }
        node.pins = std::move(ordered);
    }
    return true;
}

bool LXGraph::MatchesGroupInstance(const Node& node) const
{
    const LXGroupDefinition* group = FindGroup(node.groupId);
    if (node.type != "LX_GROUP" || !group || node.pins.size() != group->sockets.size() || node.dynamicPins ||
        !node.properties.empty())
    {
        return false;
    }
    for (std::size_t index = 0; index < node.pins.size(); ++index)
    {
        const Pin& pin = node.pins[index];
        const LXGroupSocket& socket = group->sockets[index];
        if (pin.interfaceId != socket.id || pin.name != socket.name || pin.Identifier() != socket.identifier ||
            pin.direction != socket.direction || pin.type != socket.type || pin.multiple || pin.dynamic)
        {
            return false;
        }
    }
    return true;
}

const Node* LXGraph::FindNode(Id id) const
{
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
    return it == nodes_.end() ? nullptr : &*it;
}

const NodeLayout* LXGraph::FindLayout(Id id) const
{
    const auto found = layout_.nodes.find(id);
    return found == layout_.nodes.end() ? nullptr : &found->second;
}

const FrameLayout* LXGraph::FindFrame(Id id) const
{
    const auto found = layout_.frames.find(id);
    return found == layout_.frames.end() ? nullptr : &found->second;
}

bool LXGraph::IsNodeEditable(Id id) const
{
    const Node* node = FindNode(id);
    return node && CanEditNode(*node);
}

const Pin* LXGraph::FindPin(Id id) const
{
    for (const auto& node : nodes_)
    {
        for (const auto& pin : node.pins)
        {
            if (pin.id == id)
            {
                return &pin;
            }
        }
    }
    return nullptr;
}

LXGraph::Snapshot LXGraph::Capture() const
{
    return {nodes_, links_, layout_, groups_, nextId_};
}
void LXGraph::Commit()
{
    undo_.push_back(Capture());
    if (undo_.size() > kUndoLimit)
    {
        undo_.erase(undo_.begin());
    }
    redo_.clear();
}
void LXGraph::Restore(const Snapshot& snapshot)
{
    const ViewLayout currentView = layout_.view;
    nodes_ = snapshot.nodes;
    links_ = snapshot.links;
    layout_ = snapshot.layout;
    layout_.view = currentView;
    groups_ = snapshot.groups;
    nextId_ = snapshot.nextId;
}

Id LXGraph::AddNode(const NodeSpec& spec, float x, float y)
{
    if (spec.type.empty() || spec.type == "LX_GROUP" || IsGroupBoundaryType(spec.type) || !std::isfinite(x) ||
        !std::isfinite(y) || spec.pins.size() >= std::numeric_limits<Id>::max() - nextId_ ||
        (definitions_ && !definitions_->MatchesSpec(domain_, spec)) ||
        std::any_of(spec.pins.begin(), spec.pins.end(),
                    [&](const Pin& pin) {
                        return pin.interfaceId || !AllowsPin(pin.type) || !IsSocketValueValid(pin.type, pin.value);
                    }) ||
        (spec.dynamicPins && !AllowsPin(spec.dynamicPins->type)))
    {
        return 0;
    }
    Commit();
    Node node;
    node.id = nextId_++;
    node.type = spec.type;
    node.title = spec.title;
    node.properties = spec.properties;
    node.dynamicPins = spec.dynamicPins;
    for (auto pin : spec.pins)
    {
        pin.id = nextId_++;
        pin.node = node.id;
        pin.identifier = pin.Identifier();
        node.pins.push_back(std::move(pin));
    }
    const Id id = node.id;
    nodes_.push_back(std::move(node));
    layout_.nodes.emplace(id, NodeLayout{x, y, false});
    return id;
}

Id LXGraph::CreateNode(const std::string& type, float x, float y)
{
    const LXNodeDefinition* definition = definitions_ ? definitions_->Find(type) : nullptr;
    return definition ? AddNode(definition->defaults, x, y) : 0;
}

Id LXGraph::AddGroupBoundaryNode(Direction externalSide, float x, float y)
{
    if (!std::isfinite(x) || !std::isfinite(y) || nextId_ == std::numeric_limits<Id>::max())
    {
        return 0;
    }
    const char* type = externalSide == Direction::Input ? "LX_GROUP_INPUT" : "LX_GROUP_OUTPUT";
    if (std::any_of(nodes_.begin(), nodes_.end(), [&](const Node& node) { return node.type == type; }))
    {
        return 0;
    }
    Commit();
    const Id id = nextId_++;
    nodes_.push_back({id, type, externalSide == Direction::Input ? "Group Input" : "Group Output"});
    layout_.nodes.emplace(id, NodeLayout{x, y, false});
    return id;
}

Id LXGraph::AddGroupBoundaryPin(Id nodeId, std::string identifier, std::string name, PinType type, LXSocketValue value)
{
    const auto node =
        std::find_if(nodes_.begin(), nodes_.end(), [nodeId](const Node& candidate) { return candidate.id == nodeId; });
    if (node == nodes_.end() || !IsGroupBoundaryType(node->type) || !CanEditNode(*node) || identifier.empty() ||
        name.empty() || !AllowsPin(type) || !IsSocketValueValid(type, value) ||
        nextId_ == std::numeric_limits<Id>::max() || node->pins.size() >= kDocumentLimit ||
        std::any_of(node->pins.begin(), node->pins.end(),
                    [&](const Pin& pin) { return pin.Identifier() == identifier; }))
    {
        return 0;
    }
    Commit();
    Pin pin;
    pin.id = nextId_++;
    pin.node = nodeId;
    pin.name = std::move(name);
    pin.identifier = std::move(identifier);
    pin.direction = GroupBoundaryPinDirection(node->type);
    pin.type = type;
    pin.value = std::move(value);
    const Id id = pin.id;
    node->pins.push_back(std::move(pin));
    return id;
}

bool LXGraph::RemoveGroupBoundaryPin(Id pinId)
{
    for (Node& node : nodes_)
    {
        if (!IsGroupBoundaryType(node.type) || !CanEditNode(node))
        {
            continue;
        }
        const auto pin = std::find_if(node.pins.begin(), node.pins.end(),
                                      [pinId](const Pin& candidate) { return candidate.id == pinId; });
        if (pin == node.pins.end())
        {
            continue;
        }
        Commit();
        node.pins.erase(pin);
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                    [pinId](const Link& link) { return link.output == pinId || link.input == pinId; }),
                     links_.end());
        return true;
    }
    return false;
}

Id LXGraph::CreateGroup(std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets)
{
    std::unordered_set<const LXGraph*> activeGroups;
    if (name.empty() || sockets.empty() || sockets.size() > kDocumentLimit ||
        !IsAcyclicGroupTree(body, 0, activeGroups))
    {
        return 0;
    }
    const Id definitionId = std::max(nextId_, MaxNextIdInTree(body));
    if (sockets.size() + 1 >= std::numeric_limits<Id>::max() - definitionId)
    {
        return 0;
    }
    LXGraph bodyCopy(body);
    bodyCopy.definitions_ = definitions_;
    bodyCopy.undo_.clear();
    bodyCopy.redo_.clear();
    for (std::size_t index = 0; index < sockets.size(); ++index)
    {
        if (sockets[index].id)
        {
            return 0;
        }
        sockets[index].id = definitionId + 1 + index;
    }
    if (!SynchronizeGroupBoundaryPins(bodyCopy, sockets))
    {
        return 0;
    }
    LXGroupDefinition definition{definitionId, std::move(name), std::move(sockets),
                                 std::make_shared<const LXGraph>(std::move(bodyCopy))};
    if (!MatchesGroupDefinition(definition))
    {
        return 0;
    }
    const Id id = definition.id;
    LXGraph proposed(*this);
    proposed.nextId_ = definitionId + 1 + definition.sockets.size();
    proposed.groups_.emplace(id, definition);
    const auto issues = proposed.Validate();
    if (std::any_of(issues.begin(), issues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return 0;
    }
    Commit();
    nextId_ = proposed.nextId_;
    groups_.emplace(id, std::move(definition));
    return id;
}

Id LXGraph::CreateGroupInstance(Id groupId, float x, float y)
{
    const LXGroupDefinition* group = FindGroup(groupId);
    if (!group || !std::isfinite(x) || !std::isfinite(y) ||
        group->sockets.size() + 1 >= std::numeric_limits<Id>::max() - nextId_)
    {
        return 0;
    }
    Commit();
    Node node;
    node.id = nextId_++;
    node.type = "LX_GROUP";
    node.title = group->name;
    node.groupId = groupId;
    for (const LXGroupSocket& socket : group->sockets)
    {
        Pin pin;
        pin.id = nextId_++;
        pin.node = node.id;
        pin.name = socket.name;
        pin.identifier = socket.identifier;
        pin.direction = socket.direction;
        pin.type = socket.type;
        pin.value = socket.value;
        pin.interfaceId = socket.id;
        node.pins.push_back(std::move(pin));
    }
    const Id id = node.id;
    nodes_.push_back(std::move(node));
    layout_.nodes.emplace(id, NodeLayout{x, y, false});
    return id;
}

Id LXGraph::CollapseToGroup(const std::vector<Id>& nodeIds, std::string name, Id idFloor,
                            LXGroupCollapseMapping* mapping)
{
    const std::unordered_set<Id> selected(nodeIds.begin(), nodeIds.end());
    if (name.empty() || selected.empty() || selected.size() != nodeIds.size() || selected.size() > kDocumentLimit)
    {
        return 0;
    }
    float minimumX = std::numeric_limits<float>::max();
    float minimumY = std::numeric_limits<float>::max();
    float maximumX = std::numeric_limits<float>::lowest();
    Id instanceFrame = 0;
    bool commonFrame = true;
    bool firstNode = true;
    for (Id id : nodeIds)
    {
        const Node* node = FindNode(id);
        const NodeLayout* placement = FindLayout(id);
        if (!node || !placement || !CanEditNode(*node) || IsGroupBoundaryType(node->type) ||
            !std::isfinite(placement->x) || !std::isfinite(placement->y))
        {
            return 0;
        }
        minimumX = std::min(minimumX, placement->x);
        minimumY = std::min(minimumY, placement->y);
        maximumX = std::max(maximumX, placement->x);
        if (firstNode)
        {
            instanceFrame = placement->frame;
            firstNode = false;
        }
        else if (instanceFrame != placement->frame)
        {
            commonFrame = false;
        }
    }
    const float offsetX = 200.0f - minimumX;
    const float offsetY = 120.0f - minimumY;
    if (!std::isfinite(offsetX) || !std::isfinite(offsetY) || !std::isfinite(maximumX + offsetX + 220.0f))
    {
        return 0;
    }

    LXGraph body(domain_, definitions_);
    body.nextId_ = std::max(nextId_, idFloor);
    std::unordered_map<Id, Id> copiedPins;
    LXGroupCollapseMapping copiedIds;
    std::unordered_map<Id, Id> copiedFrames;
    for (const Node& original : nodes_)
    {
        if (!selected.contains(original.id))
        {
            continue;
        }
        if (original.pins.size() + 1 >= std::numeric_limits<Id>::max() - body.nextId_)
        {
            return 0;
        }
        Node copy = original;
        copy.id = body.nextId_++;
        copiedIds.nodes.emplace(original.id, copy.id);
        for (Pin& pin : copy.pins)
        {
            const Id oldPin = pin.id;
            pin.id = body.nextId_++;
            pin.node = copy.id;
            copiedPins.emplace(oldPin, pin.id);
            copiedIds.pins.emplace(oldPin, pin.id);
        }
        if (copy.groupId)
        {
            if (!body.groups_.contains(copy.groupId))
            {
                const LXGroupDefinition* source = FindGroup(copy.groupId);
                if (!source)
                {
                    return 0;
                }
                body.groups_.emplace(source->id, *source);
            }
        }
        NodeLayout placement = layout_.nodes.at(original.id);
        placement.x += offsetX;
        placement.y += offsetY;
        if (placement.frame)
        {
            auto found = copiedFrames.find(placement.frame);
            if (found == copiedFrames.end())
            {
                const FrameLayout* frame = FindFrame(placement.frame);
                if (!frame || body.nextId_ == std::numeric_limits<Id>::max())
                {
                    return 0;
                }
                FrameLayout copied = *frame;
                copied.x += offsetX;
                copied.y += offsetY;
                found = copiedFrames.emplace(placement.frame, body.nextId_++).first;
                body.layout_.frames.emplace(found->second, std::move(copied));
            }
            placement.frame = found->second;
        }
        body.layout_.nodes.emplace(copy.id, placement);
        body.nodes_.push_back(std::move(copy));
    }
    for (const Link& link : links_)
    {
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        if (!output || !input)
        {
            return 0;
        }
        if (selected.contains(output->node) && selected.contains(input->node))
        {
            if (body.nextId_ == std::numeric_limits<Id>::max())
            {
                return 0;
            }
            body.links_.push_back({body.nextId_++, copiedPins.at(link.output), copiedPins.at(link.input)});
        }
    }
    const Id inputBoundary = body.AddGroupBoundaryNode(Direction::Input, 20.0f, 120.0f);
    const Id outputBoundary = body.AddGroupBoundaryNode(Direction::Output, maximumX + offsetX + 220.0f, 120.0f);
    if (!inputBoundary || !outputBoundary)
    {
        return 0;
    }

    std::vector<LXGroupSocket> sockets;
    std::unordered_map<Id, std::size_t> inboundBySource;
    std::unordered_map<Id, std::size_t> outboundBySource;
    std::unordered_map<Id, std::size_t> inboundLinks;
    std::unordered_map<Id, std::size_t> outboundLinks;
    const auto addSocket = [&](Direction direction, const Pin& original) -> std::optional<std::size_t> {
        std::string identifier = original.Identifier();
        for (unsigned suffix = 2; std::any_of(sockets.begin(), sockets.end(),
                                              [&](const LXGroupSocket& socket) {
                                                  return socket.direction == direction &&
                                                         socket.identifier == identifier;
                                              });
             ++suffix)
        {
            identifier = original.Identifier() + "_" + std::to_string(suffix);
        }
        const Id boundary = direction == Direction::Input ? inputBoundary : outputBoundary;
        const Id boundaryPin =
            body.AddGroupBoundaryPin(boundary, identifier, original.name, original.type, original.value);
        if (!boundaryPin)
        {
            return std::nullopt;
        }
        const Id internalPin = copiedPins.at(original.id);
        const bool connected = direction == Direction::Input ? body.Connect(boundaryPin, internalPin).has_value()
                                                             : body.Connect(internalPin, boundaryPin).has_value();
        if (!connected)
        {
            return std::nullopt;
        }
        sockets.push_back({0, identifier, original.name, direction, original.type, boundaryPin, original.value});
        return sockets.size() - 1;
    };
    for (const Link& link : links_)
    {
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        if (selected.contains(input->node) && !selected.contains(output->node))
        {
            auto found = inboundBySource.find(link.output);
            if (found == inboundBySource.end())
            {
                const auto index = addSocket(Direction::Input, *input);
                if (!index)
                {
                    return 0;
                }
                found = inboundBySource.emplace(link.output, *index).first;
            }
            else if (!body.Connect(sockets[found->second].internalPin, copiedPins.at(link.input)))
            {
                return 0;
            }
            inboundLinks.emplace(link.id, found->second);
        }
    }
    for (const Node& node : nodes_)
    {
        if (!selected.contains(node.id))
        {
            continue;
        }
        for (const Pin& pin : node.pins)
        {
            if (pin.direction == Direction::Input &&
                std::none_of(links_.begin(), links_.end(), [&](const Link& link) { return link.input == pin.id; }) &&
                !addSocket(Direction::Input, pin))
            {
                return 0;
            }
        }
    }
    for (const Link& link : links_)
    {
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        if (selected.contains(output->node) && !selected.contains(input->node))
        {
            auto found = outboundBySource.find(link.output);
            if (found == outboundBySource.end())
            {
                const auto index = addSocket(Direction::Output, *output);
                if (!index)
                {
                    return 0;
                }
                found = outboundBySource.emplace(link.output, *index).first;
            }
            outboundLinks.emplace(link.id, found->second);
        }
    }
    for (const Node& node : nodes_)
    {
        if (!selected.contains(node.id))
        {
            continue;
        }
        for (const Pin& pin : node.pins)
        {
            if (pin.direction == Direction::Output &&
                std::none_of(links_.begin(), links_.end(), [&](const Link& link) { return link.output == pin.id; }) &&
                !addSocket(Direction::Output, pin))
            {
                return 0;
            }
        }
    }
    if (outboundBySource.empty() && std::none_of(sockets.begin(), sockets.end(), [](const LXGroupSocket& socket) {
            return socket.direction == Direction::Output;
        }))
    {
        return 0;
    }

    LXGraph proposed(*this);
    proposed.nextId_ = std::max(proposed.nextId_, idFloor);
    const Id group = proposed.CreateGroup(std::move(name), body, sockets);
    const Id instance = group ? proposed.CreateGroupInstance(group, minimumX, minimumY) : 0;
    if (!instance)
    {
        return 0;
    }
    const Node* grouped = proposed.FindNode(instance);
    std::vector<Id> instancePins;
    for (const Pin& pin : grouped->pins)
    {
        instancePins.push_back(pin.id);
    }
    proposed.layout_.nodes.at(instance).frame = commonFrame ? instanceFrame : 0;
    std::unordered_set<std::size_t> emittedInputs;
    std::vector<Link> rewritten;
    for (Link link : proposed.links_)
    {
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        const bool outputSelected = selected.contains(output->node);
        const bool inputSelected = selected.contains(input->node);
        if (outputSelected && inputSelected)
        {
            continue;
        }
        if (inputSelected)
        {
            const std::size_t socketIndex = inboundLinks.at(link.id);
            if (!emittedInputs.insert(socketIndex).second)
            {
                continue;
            }
            link.input = instancePins.at(socketIndex);
        }
        if (outputSelected)
        {
            link.output = instancePins.at(outboundLinks.at(link.id));
        }
        rewritten.push_back(link);
    }
    proposed.links_ = std::move(rewritten);
    proposed.nodes_.erase(std::remove_if(proposed.nodes_.begin(), proposed.nodes_.end(),
                                         [&](const Node& node) { return selected.contains(node.id); }),
                          proposed.nodes_.end());
    for (Id id : selected)
    {
        proposed.layout_.nodes.erase(id);
    }
    const auto issues = proposed.Validate();
    if (std::any_of(issues.begin(), issues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return 0;
    }
    Commit();
    nodes_ = std::move(proposed.nodes_);
    links_ = std::move(proposed.links_);
    groups_ = std::move(proposed.groups_);
    layout_ = std::move(proposed.layout_);
    nextId_ = proposed.nextId_;
    if (mapping)
    {
        *mapping = std::move(copiedIds);
    }
    return instance;
}

bool LXGraph::ReplaceGroupBody(Id groupId, const LXGraph& body)
{
    const LXGroupDefinition* existing = FindGroup(groupId);
    return existing && UpdateGroup(groupId, existing->name, body, existing->sockets);
}

bool LXGraph::UpdateGroup(Id groupId, std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets)
{
    LXGraph candidate(*this);
    if (!candidate.UpdateGroupLocal(groupId, std::move(name), body, std::move(sockets)))
    {
        return false;
    }
    if (!candidate.SynchronizeGroupCopies())
    {
        return false;
    }
    const auto issues = candidate.Validate();
    if (std::any_of(issues.begin(), issues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return false;
    }
    Commit();
    nodes_ = std::move(candidate.nodes_);
    links_ = std::move(candidate.links_);
    groups_ = std::move(candidate.groups_);
    layout_ = std::move(candidate.layout_);
    nextId_ = candidate.nextId_;
    return true;
}

bool LXGraph::SynchronizeGroupCopies()
{
    std::vector<Id> order;
    if (CheckGroupReferences(*this, &order) != GroupReferenceIssue::None)
    {
        return false;
    }
    for (Id id : order)
    {
        const auto rootDefinition = groups_.find(id);
        if (rootDefinition == groups_.end())
        {
            continue;
        }
        const LXGroupDefinition source = rootDefinition->second;
        bool changed = false;
        if (!SynchronizeGroupCopies(id, source, changed))
        {
            return false;
        }
    }
    return true;
}

bool LXGraph::SynchronizeGroupCopies(Id id, const LXGroupDefinition& source, bool& changed)
{
    const auto existing = groups_.find(id);
    if (existing != groups_.end() &&
        (existing->second.name != source.name || existing->second.sockets != source.sockets ||
         !existing->second.body->Equals(*source.body)))
    {
        if (!UpdateGroupLocal(id, source.name, *source.body, source.sockets, true))
        {
            return false;
        }
        changed = true;
    }
    for (auto& [nestedId, group] : groups_)
    {
        LXGraph body(*group.body);
        bool bodyChanged = false;
        if (!body.SynchronizeGroupCopies(id, source, bodyChanged))
        {
            return false;
        }
        if (bodyChanged)
        {
            group.body = std::make_shared<const LXGraph>(std::move(body));
            nextId_ = std::max(nextId_, MaxNextIdInTree(*group.body));
            changed = true;
        }
    }
    return true;
}

bool LXGraph::UpdateGroupLocal(Id groupId, std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets,
                               bool allowAssignedNewIds)
{
    const LXGroupDefinition* existing = FindGroup(groupId);
    if (!existing || !existing->body || name.empty() || sockets.empty() || sockets.size() > kDocumentLimit)
    {
        return false;
    }
    std::unordered_map<Id, LXGroupSocket> oldSockets;
    for (const LXGroupSocket& socket : existing->sockets)
    {
        oldSockets.emplace(socket.id, socket);
    }
    Id next = nextId_;
    std::unordered_set<Id> socketIds;
    for (LXGroupSocket& socket : sockets)
    {
        if (!socket.id)
        {
            if (next == std::numeric_limits<Id>::max())
            {
                return false;
            }
            socket.id = next++;
        }
        else if (!oldSockets.contains(socket.id))
        {
            if (!allowAssignedNewIds || socket.id == std::numeric_limits<Id>::max())
            {
                return false;
            }
            next = std::max(next, socket.id + 1);
        }
        if (!socketIds.insert(socket.id).second)
        {
            return false;
        }
    }
    LXGraph bodyCopy(body);
    bodyCopy.definitions_ = definitions_;
    bodyCopy.undo_.clear();
    bodyCopy.redo_.clear();
    if (!SynchronizeGroupBoundaryPins(bodyCopy, sockets))
    {
        return false;
    }
    LXGroupDefinition replacement{groupId, std::move(name), std::move(sockets),
                                  std::make_shared<const LXGraph>(std::move(bodyCopy))};
    if (!MatchesGroupDefinition(replacement) ||
        (existing->name == replacement.name && existing->sockets == replacement.sockets &&
         existing->body->Equals(*replacement.body)))
    {
        return false;
    }

    std::vector<Node> updatedNodes = nodes_;
    for (Node& node : updatedNodes)
    {
        if (node.groupId != groupId)
        {
            continue;
        }
        if (!MatchesGroupInstance(node))
        {
            return false;
        }
        std::unordered_map<Id, Pin> oldPins;
        for (const Pin& pin : node.pins)
        {
            oldPins.emplace(pin.interfaceId, pin);
            if (!socketIds.contains(pin.interfaceId) &&
                std::any_of(links_.begin(), links_.end(),
                            [&](const Link& link) { return link.output == pin.id || link.input == pin.id; }))
            {
                return false;
            }
        }
        std::vector<Pin> updatedPins;
        updatedPins.reserve(replacement.sockets.size());
        for (const LXGroupSocket& socket : replacement.sockets)
        {
            Pin pin;
            const auto previous = oldPins.find(socket.id);
            if (previous != oldPins.end())
            {
                pin = previous->second;
                const LXGroupSocket& oldSocket = oldSockets.at(socket.id);
                if ((pin.direction != socket.direction || pin.type != socket.type) &&
                    std::any_of(links_.begin(), links_.end(),
                                [&](const Link& link) { return link.output == pin.id || link.input == pin.id; }))
                {
                    return false;
                }
                if (pin.type != socket.type || pin.direction != socket.direction || pin.value == oldSocket.value)
                {
                    pin.value = socket.value;
                }
            }
            else
            {
                if (next == std::numeric_limits<Id>::max())
                {
                    return false;
                }
                pin.id = next++;
                pin.node = node.id;
                pin.value = socket.value;
            }
            pin.name = socket.name;
            pin.identifier = socket.identifier;
            pin.direction = socket.direction;
            pin.type = socket.type;
            pin.interfaceId = socket.id;
            updatedPins.push_back(std::move(pin));
        }
        node.title = replacement.name;
        node.pins = std::move(updatedPins);
    }

    LXGraph proposed(*this);
    proposed.groups_.at(groupId) = replacement;
    proposed.nodes_ = std::move(updatedNodes);
    proposed.nextId_ = std::max(next, MaxNextIdInTree(*replacement.body));
    for (const LXGroupSocket& socket : replacement.sockets)
    {
        if (socket.id == std::numeric_limits<Id>::max())
        {
            return false;
        }
        proposed.nextId_ = std::max(proposed.nextId_, socket.id + 1);
    }
    const auto issues = proposed.Validate(false);
    if (std::any_of(issues.begin(), issues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return false;
    }
    Commit();
    groups_.at(groupId) = std::move(proposed.groups_.at(groupId));
    nodes_ = std::move(proposed.nodes_);
    nextId_ = proposed.nextId_;
    return true;
}

Id LXGraph::AddConnectedNode(const NodeSpec& spec, float x, float y, Id existingPin, std::size_t specPinIndex)
{
    const Pin* existing = FindPin(existingPin);
    const Node* sourceNode = existing ? FindNode(existing->node) : nullptr;
    if (!existing || !sourceNode || !CanEditNode(*sourceNode) || spec.type.empty() || spec.type == "LX_GROUP" ||
        IsGroupBoundaryType(spec.type) || !std::isfinite(x) || !std::isfinite(y) ||
        (definitions_ && !definitions_->MatchesSpec(domain_, spec)) ||
        std::any_of(spec.pins.begin(), spec.pins.end(),
                    [&](const Pin& pin) {
                        return pin.interfaceId || !AllowsPin(pin.type) || !IsSocketValueValid(pin.type, pin.value);
                    }) ||
        (spec.dynamicPins && !AllowsPin(spec.dynamicPins->type)) || specPinIndex >= spec.pins.size() ||
        spec.pins.size() + 2 >= std::numeric_limits<Id>::max() - nextId_)
    {
        return 0;
    }
    const Pin& target = spec.pins[specPinIndex];
    if (existing->direction == target.direction || existing->type != target.type ||
        (existing->direction == Direction::Input && !existing->multiple &&
         std::any_of(links_.begin(), links_.end(), [&](const Link& link) { return link.input == existingPin; })))
    {
        return 0;
    }
    const Direction existingDirection = existing->direction;
    Commit();
    Node node;
    node.id = nextId_++;
    node.type = spec.type;
    node.title = spec.title;
    node.properties = spec.properties;
    node.dynamicPins = spec.dynamicPins;
    for (Pin pin : spec.pins)
    {
        pin.id = nextId_++;
        pin.node = node.id;
        pin.identifier = pin.Identifier();
        node.pins.push_back(std::move(pin));
    }
    const Id newPin = node.pins[specPinIndex].id;
    const Id nodeId = node.id;
    nodes_.push_back(std::move(node));
    layout_.nodes.emplace(nodeId, NodeLayout{x, y, false});
    links_.push_back({nextId_++, existingDirection == Direction::Output ? existingPin : newPin,
                      existingDirection == Direction::Input ? existingPin : newPin});
    return nodeId;
}

bool LXGraph::RemoveNode(Id id)
{
    return RemoveNodes({id});
}

bool LXGraph::RemoveNodes(const std::vector<Id>& ids)
{
    std::unordered_set<Id> removed(ids.begin(), ids.end());
    if (std::any_of(nodes_.begin(), nodes_.end(),
                    [&](const Node& node) { return removed.contains(node.id) && !CanEditNode(node); }))
    {
        return false;
    }
    if (removed.empty() ||
        std::none_of(nodes_.begin(), nodes_.end(), [&](const Node& node) { return removed.contains(node.id); }))
    {
        return false;
    }
    Commit();
    std::unordered_set<Id> pins;
    for (const Node& node : nodes_)
    {
        if (removed.contains(node.id))
        {
            for (const Pin& pin : node.pins)
            {
                pins.insert(pin.id);
            }
        }
    }
    links_.erase(
        std::remove_if(links_.begin(), links_.end(),
                       [&](const Link& link) { return pins.contains(link.output) || pins.contains(link.input); }),
        links_.end());
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(), [&](const Node& node) { return removed.contains(node.id); }),
        nodes_.end());
    for (Id id : removed)
    {
        layout_.nodes.erase(id);
    }
    return true;
}

Id LXGraph::AddDynamicPin(Id nodeId, const std::string& name)
{
    const auto it =
        std::find_if(nodes_.begin(), nodes_.end(), [nodeId](const Node& node) { return node.id == nodeId; });
    if (it == nodes_.end() || !CanEditNode(*it) || !it->dynamicPins || name.empty() ||
        nextId_ == std::numeric_limits<Id>::max())
    {
        return 0;
    }
    const DynamicPinRule& rule = *it->dynamicPins;
    const std::size_t count = static_cast<std::size_t>(
        std::count_if(it->pins.begin(), it->pins.end(), [](const Pin& pin) { return pin.dynamic; }));
    if (count >= rule.maxCount || std::any_of(it->pins.begin(), it->pins.end(), [&](const Pin& pin) {
            return pin.direction == rule.direction && pin.Identifier() == name;
        }))
    {
        return 0;
    }
    Commit();
    const Id id = nextId_++;
    it->pins.push_back({id, nodeId, name, rule.direction, rule.type, rule.multiple, true, name});
    return id;
}

bool LXGraph::RemoveDynamicPin(Id pinId)
{
    for (Node& node : nodes_)
    {
        const auto it =
            std::find_if(node.pins.begin(), node.pins.end(), [pinId](const Pin& pin) { return pin.id == pinId; });
        if (it == node.pins.end() || !it->dynamic || !CanEditNode(node))
        {
            continue;
        }
        Commit();
        node.pins.erase(it);
        links_.erase(std::remove_if(links_.begin(), links_.end(),
                                    [&](const Link& link) { return link.output == pinId || link.input == pinId; }),
                     links_.end());
        return true;
    }
    return false;
}

bool LXGraph::MoveDynamicPin(Id pinId, int direction)
{
    if (direction != -1 && direction != 1)
    {
        return false;
    }
    for (Node& node : nodes_)
    {
        for (std::size_t index = 0; index < node.pins.size(); ++index)
        {
            if (node.pins[index].id != pinId || !node.pins[index].dynamic || !CanEditNode(node))
            {
                continue;
            }
            const std::ptrdiff_t neighbor = static_cast<std::ptrdiff_t>(index) + direction;
            if (neighbor < 0 || neighbor >= static_cast<std::ptrdiff_t>(node.pins.size()) ||
                !node.pins[neighbor].dynamic)
            {
                return false;
            }
            Commit();
            std::swap(node.pins[index], node.pins[neighbor]);
            return true;
        }
    }
    return false;
}

GraphFragment LXGraph::CopyNodes(const std::vector<Id>& ids) const
{
    GraphFragment fragment;
    const std::unordered_set<Id> selected(ids.begin(), ids.end());
    std::unordered_set<Id> copiedPins;
    for (const Node& node : nodes_)
    {
        if (!selected.contains(node.id))
        {
            continue;
        }
        fragment.nodes.push_back(node);
        fragment.layout.nodes.emplace(node.id, layout_.nodes.at(node.id));
        if (node.groupId)
        {
            fragment.groups.emplace(node.groupId, groups_.at(node.groupId));
        }
        const Id frameId = layout_.nodes.at(node.id).frame;
        if (frameId)
        {
            fragment.layout.frames.emplace(frameId, layout_.frames.at(frameId));
        }
        for (const Pin& pin : node.pins)
        {
            copiedPins.insert(pin.id);
        }
    }
    for (const Link& link : links_)
    {
        if (copiedPins.contains(link.output) && copiedPins.contains(link.input))
        {
            fragment.links.push_back(link);
        }
    }
    return fragment;
}

std::vector<Id> LXGraph::PasteNodes(const GraphFragment& fragment, float offsetX, float offsetY)
{
    std::vector<Id> added;
    if (fragment.nodes.empty() || !std::isfinite(offsetX) || !std::isfinite(offsetY) ||
        std::any_of(fragment.nodes.begin(), fragment.nodes.end(),
                    [](const Node& node) { return IsGroupBoundaryType(node.type); }))
    {
        return added;
    }
    std::size_t required = fragment.nodes.size() + fragment.links.size();
    std::unordered_set<Id> originalPins;
    std::unordered_set<Id> originalNodes;
    if (fragment.layout.nodes.size() != fragment.nodes.size())
    {
        return added;
    }
    for (const Node& node : fragment.nodes)
    {
        if (node.groupId)
        {
            const auto source = fragment.groups.find(node.groupId);
            const LXGroupDefinition* target = FindGroup(node.groupId);
            if (source == fragment.groups.end() || !target || source->second.name != target->name ||
                source->second.sockets != target->sockets || !source->second.body || !target->body ||
                !source->second.body->Equals(*target->body))
            {
                return added;
            }
        }
        if (!CanEditNode(node) || !originalNodes.insert(node.id).second || !fragment.layout.nodes.contains(node.id) ||
            !std::isfinite(fragment.layout.nodes.at(node.id).x + offsetX) ||
            !std::isfinite(fragment.layout.nodes.at(node.id).y + offsetY))
        {
            return added;
        }
        required += node.pins.size();
        for (const Pin& pin : node.pins)
        {
            if (!pin.id || !originalPins.insert(pin.id).second)
            {
                return added;
            }
        }
    }
    if (std::any_of(fragment.links.begin(), fragment.links.end(), [&](const Link& link) {
            return !originalPins.contains(link.output) || !originalPins.contains(link.input);
        }))
    {
        return added;
    }
    if (required >= std::numeric_limits<Id>::max() - nextId_)
    {
        return added;
    }
    const auto append = [&](LXGraph& graph) {
        std::vector<Id> inserted;
        std::unordered_map<Id, Id> pinIds;
        inserted.reserve(fragment.nodes.size());
        for (Node copy : fragment.nodes)
        {
            const Id oldNode = copy.id;
            copy.id = graph.nextId_++;
            NodeLayout placement = fragment.layout.nodes.at(oldNode);
            placement.x += offsetX;
            placement.y += offsetY;
            if (placement.frame)
            {
                const auto sourceFrame = fragment.layout.frames.find(placement.frame);
                const auto targetFrame = graph.layout_.frames.find(placement.frame);
                if (sourceFrame == fragment.layout.frames.end() || targetFrame == graph.layout_.frames.end() ||
                    sourceFrame->second != targetFrame->second)
                {
                    placement.frame = 0;
                }
            }
            for (Pin& pin : copy.pins)
            {
                const Id oldPin = pin.id;
                pin.id = graph.nextId_++;
                pin.node = copy.id;
                pinIds.emplace(oldPin, pin.id);
            }
            inserted.push_back(copy.id);
            graph.layout_.nodes.emplace(copy.id, placement);
            graph.nodes_.push_back(std::move(copy));
        }
        for (const Link& link : fragment.links)
        {
            graph.links_.push_back({graph.nextId_++, pinIds.at(link.output), pinIds.at(link.input)});
        }
        return inserted;
    };
    LXGraph candidate(*this);
    append(candidate);
    const auto issues = candidate.Validate();
    if (std::any_of(issues.begin(), issues.end(),
                    [](const Issue& issue) { return issue.severity == Issue::Severity::Error; }))
    {
        return added;
    }
    Commit();
    return append(*this);
}

bool LXGraph::SetNodePosition(Id id, float x, float y, bool undoable)
{
    return SetNodePositions({{id, x, y}}, undoable);
}

bool LXGraph::SetNodePositions(const std::vector<NodePosition>& positions, bool undoable)
{
    bool changed = false;
    for (const NodePosition& position : positions)
    {
        const NodeLayout* layout = FindLayout(position.id);
        if (!layout || !std::isfinite(position.x) || !std::isfinite(position.y))
        {
            return false;
        }
        if (layout->x != position.x || layout->y != position.y)
        {
            changed = true;
        }
    }
    if (!changed)
    {
        return false;
    }
    if (undoable)
    {
        Commit();
    }
    for (const NodePosition& position : positions)
    {
        NodeLayout& layout = layout_.nodes.at(position.id);
        layout.x = position.x;
        layout.y = position.y;
    }
    return true;
}

bool LXGraph::SetNodeCollapsed(Id id, bool collapsed)
{
    const NodeLayout* layout = FindLayout(id);
    if (!layout || layout->collapsed == collapsed)
    {
        return false;
    }
    Commit();
    layout_.nodes.at(id).collapsed = collapsed;
    return true;
}

Id LXGraph::AddFrame(std::string label, float x, float y, float width, float height, const std::vector<Id>& members)
{
    if (label.empty() || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.0f || height <= 0.0f || nextId_ == std::numeric_limits<Id>::max())
    {
        return 0;
    }
    std::unordered_set<Id> unique;
    for (Id member : members)
    {
        if (!FindNode(member) || !unique.insert(member).second)
        {
            return 0;
        }
    }
    Commit();
    const Id id = nextId_++;
    layout_.frames.emplace(id, FrameLayout{std::move(label), x, y, width, height});
    for (Id member : members)
    {
        layout_.nodes.at(member).frame = id;
    }
    return id;
}

bool LXGraph::RenameFrame(Id id, std::string label)
{
    const FrameLayout* frame = FindFrame(id);
    if (!frame || label.empty() || frame->label == label)
    {
        return false;
    }
    Commit();
    layout_.frames.at(id).label = std::move(label);
    return true;
}

bool LXGraph::RemoveFrame(Id id)
{
    if (!FindFrame(id))
    {
        return false;
    }
    Commit();
    for (auto& [node, placement] : layout_.nodes)
    {
        if (placement.frame == id)
        {
            placement.frame = 0;
        }
    }
    layout_.frames.erase(id);
    return true;
}

bool LXGraph::MoveFrame(Id id, float x, float y, bool undoable)
{
    const FrameLayout* frame = FindFrame(id);
    if (!frame || !std::isfinite(x) || !std::isfinite(y) || (frame->x == x && frame->y == y))
    {
        return false;
    }
    const float dx = x - frame->x;
    const float dy = y - frame->y;
    for (const auto& [node, placement] : layout_.nodes)
    {
        if (placement.frame == id && (!std::isfinite(placement.x + dx) || !std::isfinite(placement.y + dy)))
        {
            return false;
        }
    }
    if (undoable)
    {
        Commit();
    }
    FrameLayout& moved = layout_.frames.at(id);
    moved.x = x;
    moved.y = y;
    for (auto& [node, placement] : layout_.nodes)
    {
        if (placement.frame == id)
        {
            placement.x += dx;
            placement.y += dy;
        }
    }
    return true;
}

bool LXGraph::ResizeFrame(Id id, float width, float height)
{
    const FrameLayout* frame = FindFrame(id);
    if (!frame || !std::isfinite(width) || !std::isfinite(height) || width <= 0.0f || height <= 0.0f ||
        (frame->width == width && frame->height == height))
    {
        return false;
    }
    Commit();
    FrameLayout& resized = layout_.frames.at(id);
    resized.width = width;
    resized.height = height;
    return true;
}

bool LXGraph::SetNodeFrame(Id node, Id frame)
{
    const NodeLayout* placement = FindLayout(node);
    if (!placement || (frame && !FindFrame(frame)) || placement->frame == frame)
    {
        return false;
    }
    Commit();
    layout_.nodes.at(node).frame = frame;
    return true;
}

bool LXGraph::SetView(ViewLayout view)
{
    if (!std::isfinite(view.centerX) || !std::isfinite(view.centerY) || !std::isfinite(view.zoom) ||
        view.zoom <= 0.0f || layout_.view == view)
    {
        return false;
    }
    layout_.view = view;
    return true;
}

bool LXGraph::SetProperty(Id id, const std::string& key, const std::string& value)
{
    const auto it = std::find_if(nodes_.begin(), nodes_.end(), [id](const Node& node) { return node.id == id; });
    if (it == nodes_.end() || key.empty() || !CanEditNode(*it))
    {
        return false;
    }
    const auto existing = it->properties.find(key);
    if (existing != it->properties.end() && existing->second == value)
    {
        return false;
    }
    Commit();
    it->properties[key] = value;
    return true;
}

bool LXGraph::SetSocketValue(Id pinId, LXSocketValue value)
{
    for (Node& node : nodes_)
    {
        for (Pin& pin : node.pins)
        {
            if (pin.id != pinId)
            {
                continue;
            }
            if (!CanEditNode(node) || !IsSocketValueValid(pin.type, value) || pin.value == value ||
                (pin.direction == Direction::Input &&
                 std::any_of(links_.begin(), links_.end(), [pinId](const Link& link) { return link.input == pinId; })))
            {
                return false;
            }
            Commit();
            pin.value = std::move(value);
            return true;
        }
    }
    return false;
}

bool LXGraph::WouldCycle(Id outputNode, Id inputNode) const
{
    if (outputNode == inputNode)
    {
        return true;
    }
    std::vector<Id> stack{inputNode};
    std::unordered_set<Id> seen;
    while (!stack.empty())
    {
        const Id current = stack.back();
        stack.pop_back();
        if (!seen.insert(current).second)
        {
            continue;
        }
        if (current == outputNode)
        {
            return true;
        }
        for (const auto& link : links_)
        {
            const Pin* source = FindPin(link.output);
            const Pin* target = FindPin(link.input);
            if (source && target && source->node == current)
            {
                stack.push_back(target->node);
            }
        }
    }
    return false;
}

    bool LXGraph::CanConnect(Id first, Id second, std::string* reason) const
    {
        return CanConnect(first, second, false, reason);
    }

    bool LXGraph::CanConnect(Id first, Id second, bool replaceInput, std::string* reason) const
    {
        const Pin* a = FindPin(first);
        const Pin* b = FindPin(second);
        if (!a || !b)
        {
            return Fail(reason, "Missing pin");
        }
        if (a->direction == b->direction)
        {
            return Fail(reason, "Connect output to input");
        }
        const Pin* output = a->direction == Direction::Output ? a : b;
        const Pin* input = a->direction == Direction::Input ? a : b;
        const Node* outputNode = FindNode(output->node);
        const Node* inputNode = FindNode(input->node);
        if (!outputNode || !inputNode || !CanEditNode(*outputNode) || !CanEditNode(*inputNode) ||
            !AllowsPin(output->type) || !AllowsPin(input->type))
        {
            return Fail(reason, "Node or pin is not editable in this domain");
        }
        if (output->type != input->type)
        {
            return Fail(reason, "Pin types differ");
        }
        if (output->node == input->node)
        {
            return Fail(reason, "Self links are not allowed");
        }
        if (!replaceInput && !input->multiple &&
            std::any_of(links_.begin(), links_.end(), [&](const Link& link) { return link.input == input->id; }))
        {
            return Fail(reason, "Input already connected");
        }
        if (std::any_of(links_.begin(), links_.end(),
                        [&](const Link& link) { return link.output == output->id && link.input == input->id; }))
        {
            return Fail(reason, "Duplicate link");
        }
        if (replaceInput && !input->multiple)
        {
            for (const Link& link : links_)
            {
                if (link.input == input->id)
                {
                    const Pin* previousOutput = FindPin(link.output);
                    if (!previousOutput || !IsNodeEditable(previousOutput->node))
                    {
                        return Fail(reason, "Existing link is not editable in this domain");
                    }
                }
            }
        }
        if (!(domain_ == "animation" && output->type == PinType::Flow) && WouldCycle(output->node, input->node))
        {
            return Fail(reason, "Cycle is not allowed");
        }
        if (reason)
        {
            reason->clear();
        }
        return true;
    }

std::optional<Id> LXGraph::Connect(Id first, Id second, std::string* reason)
{
    if (!CanConnect(first, second, reason) || nextId_ == std::numeric_limits<Id>::max())
    {
        return std::nullopt;
    }
    const Pin* a = FindPin(first);
    Commit();
    const Id id = nextId_++;
    links_.push_back(
        {id, a->direction == Direction::Output ? first : second, a->direction == Direction::Input ? first : second});
    return id;
}

    std::optional<Id> LXGraph::ReplaceInputConnection(Id first, Id second, std::string* reason)
    {
        if (!CanConnect(first, second, true, reason))
        {
            return std::nullopt;
        }
        if (nextId_ == std::numeric_limits<Id>::max())
        {
            Fail(reason, "No link IDs available");
            return std::nullopt;
        }
        const Pin* a = FindPin(first);
        const Pin* b = FindPin(second);
        const Pin* output = a->direction == Direction::Output ? a : b;
        const Pin* input = a->direction == Direction::Input ? a : b;
        Commit();
        if (!input->multiple)
        {
            std::erase_if(links_, [&](const Link& link) { return link.input == input->id; });
        }
        const Id id = nextId_++;
        links_.push_back({id, output->id, input->id});
        return id;
    }

bool LXGraph::Disconnect(Id id)
{
    const auto it = std::find_if(links_.begin(), links_.end(), [id](const Link& link) { return link.id == id; });
    if (it == links_.end())
    {
        return false;
    }
    const Pin* output = FindPin(it->output);
    const Pin* input = FindPin(it->input);
    if (!output || !input || !IsNodeEditable(output->node) || !IsNodeEditable(input->node))
    {
        return false;
    }
    Commit();
    links_.erase(it);
    return true;
}

std::vector<Issue> LXGraph::Validate(bool checkSharedCopies) const
{
    std::vector<Issue> issues;
    std::unordered_set<const LXGraph*> activeGroups;
    if (!IsAcyclicGroupTree(*this, 0, activeGroups))
    {
        issues.push_back({"group_cycle", "Group ownership contains a cycle or exceeds nesting limit"});
        return issues;
    }
    switch (CheckGroupReferences(*this))
    {
    case GroupReferenceIssue::Cycle:
        issues.push_back({"group_reference_cycle", "Group definitions reference each other cyclically"});
        return issues;
    case GroupReferenceIssue::Depth:
        issues.push_back({"group_reference_depth", "Group reference chain exceeds the nesting limit"});
        return issues;
    case GroupReferenceIssue::Missing:
        issues.push_back({"group_reference_missing", "Group reference has no definition"});
        return issues;
    case GroupReferenceIssue::None:
        break;
    }
    if (checkSharedCopies)
    {
        std::map<Id, CanonicalGroup> canonical;
        CollectCanonicalGroups(*this, 0, canonical);
        if (!SharedGroupCopiesMatch(*this, canonical))
        {
            issues.push_back({"group_shared_definition", "Group copy differs from its shared definition"});
            return issues;
        }
    }
    std::unordered_set<Id> ids;
    std::unordered_map<Id, unsigned> fanIn;
    if (definitions_ && !definitions_->HasDomain(domain_))
    {
        issues.push_back({"unknown_domain", "Graph domain is not registered"});
    }
    if (!std::isfinite(layout_.view.centerX) || !std::isfinite(layout_.view.centerY) ||
        !std::isfinite(layout_.view.zoom) || layout_.view.zoom <= 0.0f)
    {
        issues.push_back({"view_layout", "View layout is invalid"});
    }
    for (const auto& [id, group] : groups_)
    {
        if (id != group.id || !ids.insert(id).second || !MatchesGroupDefinition(group))
        {
            issues.push_back({"group_definition", "Group definition or body is invalid", id, 0});
        }
        for (const LXGroupSocket& socket : group.sockets)
        {
            if (!socket.id || !ids.insert(socket.id).second)
            {
                issues.push_back({"group_interface_id", "Group interface ID is invalid or duplicated", id, socket.id});
            }
        }
    }
    for (const auto& node : nodes_)
    {
        const NodeLayout* placement = FindLayout(node.id);
        if (!placement || !std::isfinite(placement->x) || !std::isfinite(placement->y))
        {
            issues.push_back({"node_layout", "Node layout is missing or invalid", node.id, 0});
        }
        else if (placement->frame && !FindFrame(placement->frame))
        {
            issues.push_back({"frame_membership", "Node references a missing frame", node.id, 0});
        }
        const LXNodeDefinition* definition = definitions_ && !node.groupId ? definitions_->Find(node.type) : nullptr;
        if (node.groupId || node.type == "LX_GROUP")
        {
            if (!MatchesGroupInstance(node))
            {
                issues.push_back({"group_instance", "Group instance does not match its interface", node.id, 0});
            }
        }
        else if (IsGroupBoundaryType(node.type))
        {
            if (!CanEditNode(node))
            {
                issues.push_back({"group_boundary", "Group boundary node is invalid", node.id, 0});
            }
        }
        else if (definitions_ && !definition)
        {
            issues.push_back({"unknown_node", "Node definition is unavailable; original data is read-only", node.id, 0,
                              Issue::Severity::Warning});
        }
        else if (definition && definition->domain != domain_)
        {
            issues.push_back({"node_domain", "Node belongs to another graph domain", node.id, 0});
        }
        else if (definition && !definitions_->MatchesNode(domain_, node))
        {
            issues.push_back({"node_schema", "Node differs from its registered definition", node.id, 0});
        }
        if (!node.id || !ids.insert(node.id).second)
        {
            issues.push_back({"duplicate_id", "Invalid node ID", node.id, 0});
        }
        if (node.type.empty())
        {
            issues.push_back({"missing_type", "Node type is empty", node.id, 0});
        }
        std::size_t dynamicCount = 0;
        for (const auto& pin : node.pins)
        {
            if (!AllowsPin(pin.type))
            {
                issues.push_back({"pin_domain", "Pin type is not allowed in this graph domain", node.id, pin.id,
                                  definition || !definitions_ ? Issue::Severity::Error : Issue::Severity::Warning});
            }
            if (!pin.id || !ids.insert(pin.id).second)
            {
                issues.push_back({"duplicate_id", "Invalid pin ID", node.id, pin.id});
            }
            if (pin.node != node.id)
            {
                issues.push_back({"owner", "Pin owner differs", node.id, pin.id});
            }
            if (pin.name.empty() || pin.Identifier().empty() ||
                std::count_if(node.pins.begin(), node.pins.end(), [&](const Pin& candidate) {
                    return candidate.direction == pin.direction && candidate.Identifier() == pin.Identifier();
                }) != 1)
            {
                issues.push_back({"pin_identifier", "Pin identifier is empty or duplicated", node.id, pin.id});
            }
            if (!IsSocketValueValid(pin.type, pin.value))
            {
                issues.push_back({"socket_value", "Socket value does not match its type", node.id, pin.id});
            }
            if (!node.groupId && pin.interfaceId)
            {
                issues.push_back({"group_interface", "Ordinary pin references a group interface", node.id, pin.id});
            }
            if (pin.dynamic)
            {
                ++dynamicCount;
                if (!node.dynamicPins || pin.direction != node.dynamicPins->direction ||
                    pin.type != node.dynamicPins->type || pin.multiple != node.dynamicPins->multiple)
                {
                    issues.push_back({"dynamic_pin_rule", "Dynamic pin violates node rule", node.id, pin.id});
                }
            }
        }
        if (node.dynamicPins && dynamicCount > node.dynamicPins->maxCount)
        {
            issues.push_back({"dynamic_pin_limit", "Too many dynamic pins", node.id, 0});
        }
    }
    for (const auto& [id, placement] : layout_.nodes)
    {
        if (!FindNode(id))
        {
            issues.push_back({"orphan_layout", "Layout references a missing node", id, 0});
        }
    }
    for (const auto& [id, frame] : layout_.frames)
    {
        if (!id || !ids.insert(id).second || frame.label.empty() || !std::isfinite(frame.x) ||
            !std::isfinite(frame.y) || !std::isfinite(frame.width) || !std::isfinite(frame.height) ||
            frame.width <= 0.0f || frame.height <= 0.0f)
        {
            issues.push_back({"frame_layout", "Frame layout is invalid", id, 0});
        }
    }
    for (const auto& link : links_)
    {
        if (!link.id || !ids.insert(link.id).second)
        {
            issues.push_back({"duplicate_id", "Invalid link ID", 0, 0});
        }
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        if (!output || !input)
        {
            issues.push_back({"missing_pin", "Link references missing pin", 0, 0});
            continue;
        }
        if (output->direction != Direction::Output || input->direction != Direction::Input ||
            output->type != input->type)
        {
            issues.push_back({"invalid_link", "Link direction or type differs", input->node, input->id});
        }
        if (!input->multiple && ++fanIn[input->id] > 1)
        {
            issues.push_back({"fan_in", "Input has multiple links", input->node, input->id});
        }
    }
    // Animation flow transitions may return to prior states; data links stay acyclic.
    for (const auto& link : links_)
    {
        const Pin* output = FindPin(link.output);
        const Pin* input = FindPin(link.input);
        if (!output || !input || (domain_ == "animation" && output->type == PinType::Flow))
        {
            continue;
        }
        LXGraph remaining(*this);
        remaining.links_.erase(std::remove_if(remaining.links_.begin(), remaining.links_.end(),
                                              [&](const Link& item) { return item.id == link.id; }),
                               remaining.links_.end());
        if (remaining.WouldCycle(output->node, input->node))
        {
            issues.push_back({"cycle", "Graph contains a cycle", input->node, input->id});
            break;
        }
    }
    return issues;
}

bool LXGraph::Undo()
{
    if (undo_.empty())
    {
        return false;
    }
    redo_.push_back(Capture());
    Restore(undo_.back());
    undo_.pop_back();
    return true;
}
bool LXGraph::Redo()
{
    if (redo_.empty())
    {
        return false;
    }
    undo_.push_back(Capture());
    Restore(redo_.back());
    redo_.pop_back();
    return true;
}

const char* PinTypeName(PinType type)
{
    static constexpr const char* names[] = {"Flow",  "Bool",   "Int",     "Float",  "Vector",
                                            "Color", "Normal", "Texture", "Surface", "Sampler", "Closure", "Vector2"};
    const auto index = static_cast<unsigned>(type);
    return index < std::size(names) ? names[index] : "Unknown";
}

void LXGraph::Write(std::ostream& out) const
{
    out << "LXG 10 " << std::quoted(domain_) << ' ' << nextId_ << '\n';
    out << "N " << nodes_.size() << '\n';
    for (const Node& node : nodes_)
    {
        out << node.id << ' ' << std::quoted(node.type) << ' ' << std::quoted(node.title) << ' ' << node.pins.size()
            << ' ' << node.properties.size() << ' ' << node.dynamicPins.has_value() << ' '
            << static_cast<int>(node.dynamicPins ? node.dynamicPins->direction : Direction::Input) << ' '
            << static_cast<int>(node.dynamicPins ? node.dynamicPins->type : PinType::Float) << ' '
            << (node.dynamicPins ? node.dynamicPins->multiple : false) << ' '
            << (node.dynamicPins ? node.dynamicPins->maxCount : 0) << ' ' << node.groupId << '\n';
        for (const Pin& pin : node.pins)
        {
            out << pin.id << ' ' << std::quoted(pin.name) << ' ' << std::quoted(pin.Identifier()) << ' '
                << static_cast<int>(pin.direction) << ' ' << static_cast<int>(pin.type) << ' ' << pin.multiple << ' '
                << pin.dynamic << ' ' << pin.interfaceId;
            WriteSocketValue(out, pin.value);
            out << '\n';
        }
        for (const auto& [key, value] : node.properties)
        {
            out << std::quoted(key) << ' ' << std::quoted(value) << '\n';
        }
    }
    out << "P " << layout_.nodes.size() << '\n';
    for (const auto& [id, placement] : layout_.nodes)
    {
        out << id << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10) << placement.x << ' '
            << placement.y << ' ' << placement.collapsed << ' ' << placement.frame << '\n';
    }
    out << "F " << layout_.frames.size() << '\n';
    for (const auto& [id, frame] : layout_.frames)
    {
        out << id << ' ' << std::quoted(frame.label) << ' '
            << std::setprecision(std::numeric_limits<float>::max_digits10) << frame.x << ' ' << frame.y << ' '
            << frame.width << ' ' << frame.height << '\n';
    }
    out << "V " << layout_.view.saved << ' ' << std::setprecision(std::numeric_limits<float>::max_digits10)
        << layout_.view.centerX << ' ' << layout_.view.centerY << ' ' << layout_.view.zoom << '\n';
    out << "L " << links_.size() << '\n';
    for (const Link& link : links_)
    {
        out << link.id << ' ' << link.output << ' ' << link.input << '\n';
    }
    out << "G " << groups_.size() << '\n';
    for (const auto& [id, group] : groups_)
    {
        std::ostringstream body;
        group.body->Write(body);
        out << id << ' ' << std::quoted(group.name) << ' ' << group.sockets.size() << ' ' << std::quoted(body.str())
            << '\n';
        for (const LXGroupSocket& socket : group.sockets)
        {
            out << socket.id << ' ' << std::quoted(socket.identifier) << ' ' << std::quoted(socket.name) << ' '
                << static_cast<int>(socket.direction) << ' ' << static_cast<int>(socket.type) << ' '
                << socket.internalPin;
            WriteSocketValue(out, socket.value);
            out << '\n';
        }
    }
}

bool LXGraph::Save(const std::string& path, std::string* error) const
{
    const auto issues = Validate();
    const auto invalid = std::find_if(issues.begin(), issues.end(),
                                      [](const Issue& issue) { return issue.severity == Issue::Severity::Error; });
    if (invalid != issues.end())
    {
        return Fail(error, invalid->code + ": " + invalid->message);
    }
    const std::filesystem::path target = Utf8Path(path);
    const std::wstring suffix =
        L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(gSaveSequence.fetch_add(1));
    std::filesystem::path staged = target;
    staged += suffix;
    std::ofstream out(staged, std::ios::binary | std::ios::trunc);
    if (!out)
    {
        return Fail(error, "Cannot open output file");
    }
    Write(out);
    out.flush();
    const bool written = out.good();
    out.close();
    if (!written)
    {
        std::filesystem::remove(staged);
        return Fail(error, "Cannot write output file");
    }
    const auto stagedName = staged.u8string();
    const auto verified = LoadExact(std::string(stagedName.begin(), stagedName.end()), error, definitions_);
    if (!verified || !Equals(*verified))
    {
        std::filesystem::remove(staged);
        return Fail(error, "Staged graph did not round-trip exactly");
    }
    std::filesystem::path backup = target;
    backup += L".bak";
    std::error_code fileError;
    if (std::filesystem::exists(target, fileError))
    {
        const auto old = LoadExact(path, nullptr, nullptr);
        if (old)
        {
            std::filesystem::path stagedBackup = backup;
            stagedBackup += suffix;
            std::filesystem::copy_file(target, stagedBackup, std::filesystem::copy_options::overwrite_existing,
                                       fileError);
            if (fileError ||
                !MoveFileExW(stagedBackup.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::filesystem::remove(stagedBackup);
                std::filesystem::remove(staged);
                return Fail(error, "Cannot create graph backup");
            }
        }
    }
    if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(staged);
        return Fail(error, "Cannot replace graph file");
    }
    return true;
}

std::optional<LXGraph> LXGraph::Load(const std::string& path, std::string* error,
                                     std::shared_ptr<const LXNodeDefinitionRegistry> definitions)
{
    auto graph = LoadExact(path, error, definitions);
    if (graph)
    {
        return graph;
    }
    const std::filesystem::path target = Utf8Path(path);
    std::filesystem::path backup = target;
    backup += L".bak";
    if (!std::filesystem::exists(backup))
    {
        return std::nullopt;
    }
    const auto backupName = backup.u8string();
    graph = LoadExact(std::string(backupName.begin(), backupName.end()), error, definitions);
    if (graph && error)
    {
        *error = "Recovered graph from backup; primary file is damaged";
    }
    return graph;
}

std::optional<LXGraph> LXGraph::LoadExact(const std::string& path, std::string* error,
                                          std::shared_ptr<const LXNodeDefinitionRegistry> definitions)
{
    std::ifstream in(Utf8Path(path), std::ios::binary);
    if (!in)
    {
        Fail(error, "Cannot open input file");
        return std::nullopt;
    }
    return LoadStream(in, error, std::move(definitions), 0);
}

std::optional<LXGraph> LXGraph::LoadStream(std::istream& in, std::string* error,
                                           std::shared_ptr<const LXNodeDefinitionRegistry> definitions, unsigned depth)
{
    if (depth > kMaxGroupNesting)
    {
        Fail(error, "Group nesting limit exceeded");
        return std::nullopt;
    }
    std::string magic, domain, tag;
    int version = 0;
    Id nextId = 0;
    if (!(in >> magic >> version >> std::quoted(domain) >> nextId) || magic != "LXG" || (version < 1 || version > 10) ||
        !nextId)
    {
        Fail(error, "Invalid LXG header");
        return std::nullopt;
    }
    LXGraph graph(domain, std::move(definitions));
    graph.nextId_ = nextId;
    std::size_t count = 0;
    if (!(in >> tag >> count) || tag != "N" || count > kDocumentLimit)
    {
        Fail(error, "Invalid node count");
        return std::nullopt;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        Node node;
        NodeLayout placement;
        std::size_t pinCount = 0, propertyCount = 0;
        int collapsed = 0;
        int hasRule = 0, ruleDirection = 0, ruleType = 0, ruleMultiple = 0;
        std::size_t ruleLimit = 0;
        if (!(in >> node.id >> std::quoted(node.type) >> std::quoted(node.title)) ||
            (version <= 5 && !(in >> placement.x >> placement.y)) || !(in >> pinCount >> propertyCount) ||
            (version >= 2 && version <= 5 && !(in >> collapsed)) ||
            (version >= 3 && !(in >> hasRule >> ruleDirection >> ruleType >> ruleMultiple >> ruleLimit)) ||
            (version >= 8 && !(in >> node.groupId)) || (collapsed != 0 && collapsed != 1) ||
            (hasRule != 0 && hasRule != 1) || ruleDirection < 0 || ruleDirection > 1 || ruleType < 0 ||
            ruleType > static_cast<int>(version >= 10 ? PinType::Vector2 : version >= 9 ? PinType::Closure : PinType::Surface) ||
            (ruleMultiple != 0 && ruleMultiple != 1) ||
            pinCount > kDocumentLimit || propertyCount > kDocumentLimit || ruleLimit > kDocumentLimit)
        {
            Fail(error, "Invalid node record");
            return std::nullopt;
        }
        placement.collapsed = collapsed != 0;
        if (hasRule)
        {
            node.dynamicPins = {static_cast<Direction>(ruleDirection), static_cast<PinType>(ruleType),
                                ruleMultiple != 0, ruleLimit};
        }
        for (std::size_t p = 0; p < pinCount; ++p)
        {
            Pin pin;
            int direction = 0, type = 0, multiple = 0, dynamic = 0;
            if (!(in >> pin.id >> std::quoted(pin.name)) || (version >= 4 && !(in >> std::quoted(pin.identifier))) ||
                !(in >> direction >> type >> multiple) || (version >= 3 && !(in >> dynamic)) ||
                (version >= 8 && !(in >> pin.interfaceId)) || (version >= 5 && !ReadSocketValue(in, pin.value)) ||
                direction < 0 || direction > 1 || type < 0 ||
                type > static_cast<int>(version >= 10 ? PinType::Vector2 : version >= 9 ? PinType::Closure : PinType::Surface) ||
                (multiple != 0 && multiple != 1) || (dynamic != 0 && dynamic != 1) ||
                (version >= 4 && pin.identifier.empty()))
            {
                Fail(error, "Invalid pin record");
                return std::nullopt;
            }
            pin.node = node.id;
            pin.direction = static_cast<Direction>(direction);
            pin.type = static_cast<PinType>(type);
            pin.multiple = multiple != 0;
            pin.dynamic = dynamic != 0;
            if (version < 4)
            {
                pin.identifier = pin.name;
            }
            node.pins.push_back(std::move(pin));
        }
        for (std::size_t p = 0; p < propertyCount; ++p)
        {
            std::string key, value;
            if (!(in >> std::quoted(key) >> std::quoted(value)) || key.empty() ||
                !node.properties.emplace(key, value).second)
            {
                Fail(error, "Invalid property record");
                return std::nullopt;
            }
        }
        if (version < 3 && graph.definitions_)
        {
            const LXNodeDefinition* definition = graph.definitions_->Find(node.type);
            if (definition && definition->domain == domain)
            {
                node.dynamicPins = definition->defaults.dynamicPins;
            }
        }
        if (version <= 5)
        {
            graph.layout_.nodes.emplace(node.id, placement);
        }
        graph.nodes_.push_back(std::move(node));
    }
    if (version >= 6)
    {
        if (!(in >> tag >> count) || tag != "P" || count > kDocumentLimit)
        {
            Fail(error, "Invalid layout count");
            return std::nullopt;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            Id id = 0;
            NodeLayout placement;
            int isCollapsed = 0;
            if (!(in >> id >> placement.x >> placement.y >> isCollapsed) ||
                (version >= 7 && !(in >> placement.frame)) || (isCollapsed != 0 && isCollapsed != 1) ||
                !graph.layout_.nodes.emplace(id, placement).second)
            {
                Fail(error, "Invalid layout record");
                return std::nullopt;
            }
            graph.layout_.nodes.at(id).collapsed = isCollapsed != 0;
        }
    }
    if (version >= 7)
    {
        if (!(in >> tag >> count) || tag != "F" || count > kDocumentLimit)
        {
            Fail(error, "Invalid frame count");
            return std::nullopt;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            Id id = 0;
            FrameLayout frame;
            if (!(in >> id >> std::quoted(frame.label) >> frame.x >> frame.y >> frame.width >> frame.height) ||
                !graph.layout_.frames.emplace(id, std::move(frame)).second)
            {
                Fail(error, "Invalid frame record");
                return std::nullopt;
            }
        }
        int saved = 0;
        ViewLayout view;
        if (!(in >> tag >> saved >> view.centerX >> view.centerY >> view.zoom) || tag != "V" ||
            (saved != 0 && saved != 1))
        {
            Fail(error, "Invalid view record");
            return std::nullopt;
        }
        view.saved = saved != 0;
        graph.layout_.view = view;
    }
    if (!(in >> tag >> count) || tag != "L" || count > kDocumentLimit)
    {
        Fail(error, "Invalid link count");
        return std::nullopt;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        Link link;
        if (!(in >> link.id >> link.output >> link.input))
        {
            Fail(error, "Invalid link record");
            return std::nullopt;
        }
        graph.links_.push_back(link);
    }
    if (version >= 8)
    {
        if (!(in >> tag >> count) || tag != "G" || count > kDocumentLimit)
        {
            Fail(error, "Invalid group count");
            return std::nullopt;
        }
        for (std::size_t index = 0; index < count; ++index)
        {
            LXGroupDefinition group;
            std::size_t socketCount = 0;
            std::string bodyText;
            if (!(in >> group.id >> std::quoted(group.name) >> socketCount >> std::quoted(bodyText)) ||
                socketCount > kDocumentLimit || bodyText.size() > 64 * 1024 * 1024)
            {
                Fail(error, "Invalid group record");
                return std::nullopt;
            }
            std::istringstream bodyStream(bodyText);
            auto body = LoadStream(bodyStream, error, graph.definitions_, depth + 1);
            if (!body)
            {
                return std::nullopt;
            }
            group.body = std::make_shared<const LXGraph>(std::move(*body));
            for (std::size_t socketIndex = 0; socketIndex < socketCount; ++socketIndex)
            {
                LXGroupSocket socket;
                int direction = 0;
                int type = 0;
                if (!(in >> socket.id >> std::quoted(socket.identifier) >> std::quoted(socket.name) >> direction >>
                      type >> socket.internalPin) ||
                    !ReadSocketValue(in, socket.value) || direction < 0 || direction > 1 || type < 0 ||
                    type > static_cast<int>(version >= 10 ? PinType::Vector2 : version >= 9 ? PinType::Closure : PinType::Surface))
                {
                    Fail(error, "Invalid group socket record");
                    return std::nullopt;
                }
                socket.direction = static_cast<Direction>(direction);
                socket.type = static_cast<PinType>(type);
                group.sockets.push_back(std::move(socket));
            }
            if (!graph.groups_.emplace(group.id, std::move(group)).second)
            {
                Fail(error, "Duplicate group ID");
                return std::nullopt;
            }
        }
    }
    in >> std::ws;
    if (!in.eof())
    {
        Fail(error, "Unexpected trailing data");
        return std::nullopt;
    }
    Id maximum = 0;
    for (const auto& node : graph.nodes_)
    {
        maximum = std::max(maximum, node.id);
        for (const auto& pin : node.pins)
        {
            maximum = std::max(maximum, pin.id);
        }
    }
    for (const auto& link : graph.links_)
    {
        maximum = std::max(maximum, link.id);
    }
    for (const auto& [id, frame] : graph.layout_.frames)
    {
        maximum = std::max(maximum, id);
    }
    for (const auto& [id, group] : graph.groups_)
    {
        maximum = std::max(maximum, id);
        for (const LXGroupSocket& socket : group.sockets)
        {
            maximum = std::max(maximum, socket.id);
        }
    }
    if (graph.nextId_ <= maximum)
    {
        Fail(error, "Invalid next ID");
        return std::nullopt;
    }
    const auto issues = graph.Validate();
    const auto invalid = std::find_if(issues.begin(), issues.end(),
                                      [](const Issue& issue) { return issue.severity == Issue::Severity::Error; });
    if (invalid != issues.end())
    {
        Fail(error, invalid->code + ": " + invalid->message);
        return std::nullopt;
    }
    return graph;
}

bool LXGraph::Equals(const LXGraph& other) const
{
    if (domain_ != other.domain_ || nextId_ != other.nextId_ || layout_ != other.layout_ ||
        nodes_.size() != other.nodes_.size() || links_.size() != other.links_.size() ||
        groups_.size() != other.groups_.size())
    {
        return false;
    }
    for (const auto& [id, group] : groups_)
    {
        const auto found = other.groups_.find(id);
        if (found == other.groups_.end() || group.name != found->second.name ||
            group.sockets != found->second.sockets || !group.body || !found->second.body ||
            !group.body->Equals(*found->second.body))
        {
            return false;
        }
    }
    for (std::size_t index = 0; index < nodes_.size(); ++index)
    {
        const Node& a = nodes_[index];
        const Node& b = other.nodes_[index];
        if (a.id != b.id || a.type != b.type || a.title != b.title || a.properties != b.properties ||
            a.dynamicPins != b.dynamicPins || a.groupId != b.groupId || a.pins.size() != b.pins.size())
        {
            return false;
        }
        for (std::size_t pinIndex = 0; pinIndex < a.pins.size(); ++pinIndex)
        {
            const Pin& first = a.pins[pinIndex];
            const Pin& second = b.pins[pinIndex];
            if (first.id != second.id || first.node != second.node || first.name != second.name ||
                first.direction != second.direction || first.type != second.type || first.multiple != second.multiple ||
                first.dynamic != second.dynamic || first.Identifier() != second.Identifier() ||
                first.value != second.value || first.interfaceId != second.interfaceId)
            {
                return false;
            }
        }
    }
    for (std::size_t index = 0; index < links_.size(); ++index)
    {
        const Link& a = links_[index];
        const Link& b = other.links_[index];
        if (a.id != b.id || a.output != b.output || a.input != b.input)
        {
            return false;
        }
    }
    return true;
}

} // namespace LX
