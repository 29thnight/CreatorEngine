#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <variant>

namespace LX
{

using Id = std::uint64_t;

enum class Direction : std::uint8_t
{
    Input,
    Output
};
enum class PinType : std::uint8_t
{
    Flow,
    Bool,
    Int,
    Float,
    Vector,
    Color,
    Normal,
    Texture,
    Surface,
    Sampler,
    Closure
};

using LXSocketValue =
    std::variant<std::monostate, bool, std::int64_t, double, std::array<double, 3>, std::array<double, 4>, std::string>;

bool IsSocketValueValid(PinType type, const LXSocketValue& value);

struct Pin
{
    Id id = 0;
    Id node = 0;
    std::string name;
    Direction direction = Direction::Input;
    PinType type = PinType::Float;
    bool multiple = false;
    bool dynamic = false;
    std::string identifier;
    LXSocketValue value;
    Id interfaceId = 0;

    const std::string& Identifier() const { return identifier.empty() ? name : identifier; }
};

struct DynamicPinRule
{
    Direction direction = Direction::Input;
    PinType type = PinType::Float;
    bool multiple = false;
    std::size_t maxCount = 0;

    bool operator==(const DynamicPinRule&) const = default;
};

struct Node
{
    Id id = 0;
    std::string type;
    std::string title;
    std::vector<Pin> pins;
    std::map<std::string, std::string> properties;
    std::optional<DynamicPinRule> dynamicPins;
    Id groupId = 0;
};

struct NodeLayout
{
    float x = 0.0f;
    float y = 0.0f;
    bool collapsed = false;
    Id frame = 0;

    bool operator==(const NodeLayout&) const = default;
};

struct FrameLayout
{
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    bool operator==(const FrameLayout&) const = default;
};

struct ViewLayout
{
    bool saved = false;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float zoom = 1.0f;

    bool operator==(const ViewLayout&) const = default;
};

struct LXLayout
{
    std::map<Id, NodeLayout> nodes;
    std::map<Id, FrameLayout> frames;
    ViewLayout view;

    bool operator==(const LXLayout&) const = default;
};

struct Link
{
    Id id = 0;
    Id output = 0;
    Id input = 0;
};

class LXGraph;

struct LXGroupSocket
{
    Id id = 0;
    std::string identifier;
    std::string name;
    Direction direction = Direction::Input;
    PinType type = PinType::Float;
    Id internalPin = 0;
    LXSocketValue value;

    bool operator==(const LXGroupSocket&) const = default;
};

struct LXGroupDefinition
{
    Id id = 0;
    std::string name;
    std::vector<LXGroupSocket> sockets;
    std::shared_ptr<const LXGraph> body;
};

struct NodePosition
{
    Id id = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct GraphFragment
{
    std::vector<Node> nodes;
    std::vector<Link> links;
    LXLayout layout;
    std::map<Id, LXGroupDefinition> groups;
};

struct LXGroupCollapseMapping
{
    std::map<Id, Id> nodes;
    std::map<Id, Id> pins;
};

struct Issue
{
    enum class Severity : std::uint8_t
    {
        Error,
        Warning
    };
    std::string code;
    std::string message;
    Id node = 0;
    Id pin = 0;
    Severity severity = Severity::Error;
    Id scope = 0;
};

struct NodeSpec
{
    std::string type;
    std::string title;
    std::vector<Pin> pins;
    std::map<std::string, std::string> properties;
    std::optional<DynamicPinRule> dynamicPins;
};

class LXNodeDefinitionRegistry;

class LXGraph
{
  public:
    explicit LXGraph(std::string domain = "material",
                     std::shared_ptr<const LXNodeDefinitionRegistry> definitions = nullptr);
    const std::string& Domain() const { return domain_; }
    const std::shared_ptr<const LXNodeDefinitionRegistry>& Definitions() const { return definitions_; }
    const std::vector<Node>& Nodes() const { return nodes_; }
    const std::vector<Link>& Links() const { return links_; }
    const std::map<Id, LXGroupDefinition>& Groups() const { return groups_; }
    const LXGroupDefinition* FindGroup(Id id) const;
    const LXLayout& Layout() const { return layout_; }
    Id NextId() const { return nextId_; }
    const NodeLayout* FindLayout(Id id) const;
    const FrameLayout* FindFrame(Id id) const;
    const Node* FindNode(Id id) const;
    bool IsNodeEditable(Id id) const;
    const Pin* FindPin(Id id) const;
    Id AddNode(const NodeSpec& spec, float x, float y);
    Id CreateNode(const std::string& type, float x, float y);
    Id AddGroupBoundaryNode(Direction externalSide, float x, float y);
    Id AddGroupBoundaryPin(Id node, std::string identifier, std::string name, PinType type, LXSocketValue value = {});
    bool RemoveGroupBoundaryPin(Id pin);
    Id AddConnectedNode(const NodeSpec& spec, float x, float y, Id existingPin, std::size_t specPinIndex);
    Id CreateGroup(std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets);
    Id CreateGroupInstance(Id group, float x, float y);
    Id CollapseToGroup(const std::vector<Id>& nodes, std::string name, Id idFloor = 0,
                       LXGroupCollapseMapping* mapping = nullptr);
    bool ReplaceGroupBody(Id group, const LXGraph& body);
    bool UpdateGroup(Id group, std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets);
    bool RemoveNode(Id id);
    bool RemoveNodes(const std::vector<Id>& ids);
    Id AddDynamicPin(Id node, const std::string& name);
    bool RemoveDynamicPin(Id pin);
    bool MoveDynamicPin(Id pin, int direction);
    GraphFragment CopyNodes(const std::vector<Id>& ids) const;
    std::vector<Id> PasteNodes(const GraphFragment& fragment, float offsetX, float offsetY);
    bool SetNodePosition(Id id, float x, float y, bool undoable = true);
    bool SetNodePositions(const std::vector<NodePosition>& positions, bool undoable = true);
    bool SetNodeCollapsed(Id id, bool collapsed);
    Id AddFrame(std::string label, float x, float y, float width, float height, const std::vector<Id>& members = {});
    bool RenameFrame(Id id, std::string label);
    bool RemoveFrame(Id id);
    bool MoveFrame(Id id, float x, float y, bool undoable = true);
    bool ResizeFrame(Id id, float width, float height);
    bool SetNodeFrame(Id node, Id frame);
    bool SetView(ViewLayout view);
    bool SetProperty(Id id, const std::string& key, const std::string& value);
    bool SetSocketValue(Id pin, LXSocketValue value);
    bool CanConnect(Id first, Id second, std::string* reason = nullptr) const;
    std::optional<Id> Connect(Id first, Id second, std::string* reason = nullptr);
    std::optional<Id> ReplaceInputConnection(Id first, Id second, std::string* reason = nullptr);
    bool Disconnect(Id link);
    std::vector<Issue> Validate(bool checkSharedCopies = true) const;
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    bool Save(const std::string& path, std::string* error = nullptr) const;
    static std::optional<LXGraph> Load(const std::string& path, std::string* error = nullptr,
                                       std::shared_ptr<const LXNodeDefinitionRegistry> definitions = nullptr);
    bool Equals(const LXGraph& other) const;

  private:
    friend class LXMaterialArchive;

    struct Snapshot
    {
        std::vector<Node> nodes;
        std::vector<Link> links;
        LXLayout layout;
        std::map<Id, LXGroupDefinition> groups;
        Id nextId;
    };
    Snapshot Capture() const;
    void Commit();
    void Restore(const Snapshot& snapshot);
    bool WouldCycle(Id outputNode, Id inputNode) const;
    bool CanConnect(Id first, Id second, bool replaceInput, std::string* reason) const;
    static std::optional<LXGraph> LoadExact(const std::string& path, std::string* error,
                                            std::shared_ptr<const LXNodeDefinitionRegistry> definitions);
    static std::optional<LXGraph> LoadStream(std::istream& in, std::string* error,
                                             std::shared_ptr<const LXNodeDefinitionRegistry> definitions,
                                             unsigned depth);
    void Write(std::ostream& out) const;
    bool MatchesGroupInstance(const Node& node) const;
    bool MatchesGroupDefinition(const LXGroupDefinition& definition) const;
    bool UpdateGroupLocal(Id group, std::string name, const LXGraph& body, std::vector<LXGroupSocket> sockets,
                          bool allowAssignedNewIds = false);
    bool SynchronizeGroupCopies();
    bool SynchronizeGroupCopies(Id group, const LXGroupDefinition& source, bool& changed);
    bool SynchronizeGroupBoundaryPins(LXGraph& body, const std::vector<LXGroupSocket>& sockets) const;
    bool CanEditNode(const Node& node) const;
    bool AllowsPin(PinType type) const;
    std::string domain_;
    std::shared_ptr<const LXNodeDefinitionRegistry> definitions_;
    std::vector<Node> nodes_;
    std::vector<Link> links_;
    std::map<Id, LXGroupDefinition> groups_;
    LXLayout layout_;
    Id nextId_ = 1;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
};

const char* PinTypeName(PinType type);

} // namespace LX
