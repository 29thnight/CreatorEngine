#pragma once

#include "LXGraph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace LX
{

struct LXCreateNode
{
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXCreateNodeSpec
{
    NodeSpec spec;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXCreateGroup
{
    std::string name;
    LXGraph body;
    std::vector<LXGroupSocket> sockets;
};

struct LXCreateGroupInstance
{
    Id group = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXCollapseToGroup
{
    std::vector<Id> nodes;
    std::string name;
    Id idFloor = 0;
};

struct LXReplaceGroupBody
{
    Id group = 0;
    LXGraph body;
};

struct LXUpdateGroup
{
    Id group = 0;
    std::string name;
    LXGraph body;
    std::vector<LXGroupSocket> sockets;
};

struct LXAddGroupBoundaryNode
{
    Direction externalSide = Direction::Input;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXAddGroupBoundaryPin
{
    Id node = 0;
    std::string identifier;
    std::string name;
    PinType type = PinType::Float;
    LXSocketValue value;
};

struct LXRemoveGroupBoundaryPin
{
    Id pin = 0;
};

struct LXRemoveNode
{
    Id node = 0;
};

struct LXRemoveNodes
{
    std::vector<Id> nodes;
};

struct LXAddConnectedNode
{
    std::string type;
    float x = 0.0f;
    float y = 0.0f;
    Id existingPin = 0;
    std::size_t inputIndex = 0;
};

struct LXAddConnectedNodeSpec
{
    NodeSpec spec;
    float x = 0.0f;
    float y = 0.0f;
    Id existingPin = 0;
    std::size_t inputIndex = 0;
};

struct LXPasteNodes
{
    GraphFragment fragment;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
};

struct LXConnectPins
{
    Id first = 0;
    Id second = 0;
};

struct LXDisconnectLink
{
    Id link = 0;
};

struct LXSetSocketValue
{
    Id pin = 0;
    LXSocketValue value;
};

struct LXSetProperty
{
    Id node = 0;
    std::string key;
    std::string value;
};

struct LXSetNodePosition
{
    Id node = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXSetNodePositions
{
    std::vector<NodePosition> positions;
};

struct LXSetNodeCollapsed
{
    Id node = 0;
    bool collapsed = false;
};

struct LXAddFrame
{
    std::string label;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    std::vector<Id> members;
};

struct LXRemoveFrame
{
    Id frame = 0;
};

struct LXRenameFrame
{
    Id frame = 0;
    std::string label;
};

struct LXMoveFrame
{
    Id frame = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct LXResizeFrame
{
    Id frame = 0;
    float width = 0.0f;
    float height = 0.0f;
};

struct LXSetNodeFrame
{
    Id node = 0;
    Id frame = 0;
};

struct LXSetView
{
    ViewLayout view;
};

struct LXAddDynamicPin
{
    Id node = 0;
    std::string name;
};

struct LXRemoveDynamicPin
{
    Id pin = 0;
};

struct LXMoveDynamicPin
{
    Id pin = 0;
    int direction = 0;
};

struct LXUndo
{
};

struct LXRedo
{
};

struct LXValidate
{
};

struct LXSave
{
};

using LXCommand =
    std::variant<LXCreateNode, LXCreateNodeSpec, LXCreateGroup, LXCreateGroupInstance, LXCollapseToGroup,
                 LXReplaceGroupBody, LXUpdateGroup, LXAddGroupBoundaryNode, LXAddGroupBoundaryPin,
                 LXRemoveGroupBoundaryPin, LXRemoveNode, LXRemoveNodes, LXAddConnectedNode, LXAddConnectedNodeSpec,
                 LXPasteNodes, LXConnectPins, LXDisconnectLink, LXSetSocketValue, LXSetProperty, LXSetNodePosition,
                 LXSetNodePositions, LXSetNodeCollapsed, LXAddFrame, LXRemoveFrame, LXRenameFrame, LXMoveFrame,
                 LXResizeFrame, LXSetNodeFrame, LXSetView, LXAddDynamicPin, LXRemoveDynamicPin, LXMoveDynamicPin,
                 LXUndo, LXRedo, LXValidate, LXSave>;

struct LXCommandResult
{
    bool applied = false;
    std::string code;
    std::string message;
    std::uint64_t revision = 0;
    Id created = 0;
    std::vector<Id> createdNodes;
    std::vector<Issue> issues;
};

class LXDocument
{
  public:
    explicit LXDocument(LXGraph graph, std::string path = {}, bool persisted = false);
    LXDocument(const LXDocument&) = delete;
    LXDocument& operator=(const LXDocument&) = delete;
    LXDocument(LXDocument&&) = default;
    LXDocument& operator=(LXDocument&&) = default;

    Id DocumentId() const { return documentId_; }
    std::uint64_t Revision() const { return revision_; }
    const std::string& Path() const { return path_; }
    bool Dirty() const { return dirty_; }
    bool HasActivePreview() const { return previewActive_; }
    const LXGraph& Graph() const { return graph_; }
    LXGraph& GraphForCanvas() { return graph_; }
    LXCommandResult Execute(const LXCommand& command, std::uint64_t expectedRevision);
    bool PreviewNodePositions(const std::vector<NodePosition>& positions);
    LXCommandResult FinishNodePositionPreview(const std::vector<NodePosition>& start,
                                              const std::vector<NodePosition>& final);
    bool PreviewFramePosition(Id frame, float x, float y);
    LXCommandResult FinishFramePositionPreview(Id frame, float startX, float startY, float finalX, float finalY);
    bool Reload(std::string* error = nullptr);
    // Product archives own their persistence. A successful exact archive write
    // marks the current graph as saved without discarding its Undo history.
    bool AcceptSavedSnapshot();

    static std::optional<LXDocument> Open(const std::string& path, std::string* error = nullptr,
                                          std::shared_ptr<const LXNodeDefinitionRegistry> definitions = nullptr);

  private:
    Id documentId_ = 0;
    std::uint64_t revision_ = 0;
    std::string path_;
    LXGraph graph_;
    LXGraph saved_;
    bool dirty_ = false;
    bool unsavedInitial_ = false;
    bool previewActive_ = false;
};

} // namespace LX
