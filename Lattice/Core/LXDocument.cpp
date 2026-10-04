#include "LXDocument.h"
#include "LXNodeDefinition.h"

#include <atomic>
#include <type_traits>
#include <utility>

namespace LX
{
namespace
{
std::atomic_uint64_t gDocumentId = 1;
}

LXDocument::LXDocument(LXGraph graph, std::string path, bool persisted)
    : documentId_(gDocumentId.fetch_add(1)), path_(std::move(path)), graph_(std::move(graph)), saved_(graph_),
      dirty_(!persisted && (!graph_.Nodes().empty() || !graph_.Links().empty() || !graph_.Groups().empty() ||
                            !graph_.Layout().frames.empty() || graph_.Layout().view.saved)),
      unsavedInitial_(dirty_)
{
}

std::optional<LXDocument> LXDocument::Open(const std::string& path, std::string* error,
                                           std::shared_ptr<const LXNodeDefinitionRegistry> definitions)
{
    auto graph = LXGraph::Load(path, error, std::move(definitions));
    if (!graph)
    {
        return std::nullopt;
    }
    return LXDocument(std::move(*graph), path, true);
}

bool LXDocument::PreviewNodePositions(const std::vector<NodePosition>& positions)
{
    if (!graph_.SetNodePositions(positions, false))
    {
        return false;
    }
    previewActive_ = true;
    dirty_ = unsavedInitial_ || !graph_.Equals(saved_);
    return true;
}

LXCommandResult LXDocument::FinishNodePositionPreview(const std::vector<NodePosition>& start,
                                                      const std::vector<NodePosition>& final)
{
    graph_.SetNodePositions(start, false);
    previewActive_ = false;
    dirty_ = unsavedInitial_ || !graph_.Equals(saved_);
    return Execute(LXSetNodePositions{final}, revision_);
}

bool LXDocument::PreviewFramePosition(Id frame, float x, float y)
{
    if (!graph_.MoveFrame(frame, x, y, false))
    {
        return false;
    }
    previewActive_ = true;
    dirty_ = unsavedInitial_ || !graph_.Equals(saved_);
    return true;
}

LXCommandResult LXDocument::FinishFramePositionPreview(Id frame, float startX, float startY, float finalX, float finalY)
{
    graph_.MoveFrame(frame, startX, startY, false);
    previewActive_ = false;
    dirty_ = unsavedInitial_ || !graph_.Equals(saved_);
    return Execute(LXMoveFrame{frame, finalX, finalY}, revision_);
}

bool LXDocument::AcceptSavedSnapshot()
{
    if (previewActive_)
    {
        return false;
    }
    saved_ = graph_;
    dirty_ = false;
    unsavedInitial_ = false;
    return true;
}

bool LXDocument::Reload(std::string* error)
{
    if (path_.empty() || previewActive_)
    {
        return false;
    }
    auto loaded = LXGraph::Load(path_, error, graph_.Definitions());
    if (!loaded)
    {
        return false;
    }
    graph_ = std::move(*loaded);
    saved_ = graph_;
    unsavedInitial_ = false;
    dirty_ = false;
    ++revision_;
    return true;
}

LXCommandResult LXDocument::Execute(const LXCommand& command, std::uint64_t expectedRevision)
{
    LXCommandResult result;
    result.revision = revision_;
    if (previewActive_)
    {
        result.code = "preview_in_progress";
        return result;
    }
    if (expectedRevision != revision_)
    {
        result.code = "revision_conflict";
        result.message = "Document revision differs";
        return result;
    }

    if (std::holds_alternative<LXValidate>(command))
    {
        result.code = "ok";
        result.issues = graph_.Validate();
        return result;
    }
    if (std::holds_alternative<LXSave>(command))
    {
        if (path_.empty())
        {
            result.code = "missing_path";
            return result;
        }
        if (!graph_.Save(path_, &result.message))
        {
            result.code = "save_failed";
            return result;
        }
        saved_ = graph_;
        dirty_ = false;
        unsavedInitial_ = false;
        result.applied = true;
        result.code = "ok";
        return result;
    }

    bool changed = false;
    std::visit(
        [&](const auto& request) {
            using Request = std::decay_t<decltype(request)>;
            if constexpr (std::is_same_v<Request, LXCreateNode>)
            {
                result.created = graph_.CreateNode(request.type, request.x, request.y);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXCreateNodeSpec>)
            {
                result.created = graph_.AddNode(request.spec, request.x, request.y);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXCreateGroup>)
            {
                result.created = graph_.CreateGroup(request.name, request.body, request.sockets);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXCreateGroupInstance>)
            {
                result.created = graph_.CreateGroupInstance(request.group, request.x, request.y);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXCollapseToGroup>)
            {
                result.created = graph_.CollapseToGroup(request.nodes, request.name, request.idFloor);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXReplaceGroupBody>)
            {
                changed = graph_.ReplaceGroupBody(request.group, request.body);
            }
            else if constexpr (std::is_same_v<Request, LXUpdateGroup>)
            {
                changed = graph_.UpdateGroup(request.group, request.name, request.body, request.sockets);
            }
            else if constexpr (std::is_same_v<Request, LXAddGroupBoundaryNode>)
            {
                result.created = graph_.AddGroupBoundaryNode(request.externalSide, request.x, request.y);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXAddGroupBoundaryPin>)
            {
                result.created = graph_.AddGroupBoundaryPin(request.node, request.identifier, request.name,
                                                            request.type, request.value);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXRemoveGroupBoundaryPin>)
            {
                changed = graph_.RemoveGroupBoundaryPin(request.pin);
            }
            else if constexpr (std::is_same_v<Request, LXRemoveNode>)
            {
                changed = graph_.RemoveNode(request.node);
            }
            else if constexpr (std::is_same_v<Request, LXRemoveNodes>)
            {
                changed = graph_.RemoveNodes(request.nodes);
            }
            else if constexpr (std::is_same_v<Request, LXAddConnectedNode>)
            {
                const LXNodeDefinition* definition =
                    graph_.Definitions() ? graph_.Definitions()->Find(request.type) : nullptr;
                if (definition)
                {
                    result.created = graph_.AddConnectedNode(definition->defaults, request.x, request.y,
                                                             request.existingPin, request.inputIndex);
                    changed = result.created != 0;
                }
            }
            else if constexpr (std::is_same_v<Request, LXAddConnectedNodeSpec>)
            {
                result.created = graph_.AddConnectedNode(request.spec, request.x, request.y, request.existingPin,
                                                         request.inputIndex);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXPasteNodes>)
            {
                result.createdNodes = graph_.PasteNodes(request.fragment, request.offsetX, request.offsetY);
                changed = !result.createdNodes.empty();
                if (changed)
                {
                    result.created = result.createdNodes.front();
                }
            }
            else if constexpr (std::is_same_v<Request, LXConnectPins>)
            {
                const auto link = graph_.Connect(request.first, request.second, &result.message);
                result.created = link.value_or(0);
                changed = link.has_value();
            }
            else if constexpr (std::is_same_v<Request, LXReplaceInputConnection>)
            {
                const auto link = graph_.ReplaceInputConnection(request.first, request.second, &result.message);
                result.created = link.value_or(0);
                changed = link.has_value();
            }
            else if constexpr (std::is_same_v<Request, LXDisconnectLink>)
            {
                changed = graph_.Disconnect(request.link);
            }
            else if constexpr (std::is_same_v<Request, LXSetSocketValue>)
            {
                changed = graph_.SetSocketValue(request.pin, request.value);
            }
            else if constexpr (std::is_same_v<Request, LXSetProperty>)
            {
                changed = graph_.SetProperty(request.node, request.key, request.value);
            }
            else if constexpr (std::is_same_v<Request, LXSetNodePosition>)
            {
                changed = graph_.SetNodePosition(request.node, request.x, request.y);
            }
            else if constexpr (std::is_same_v<Request, LXSetNodePositions>)
            {
                changed = graph_.SetNodePositions(request.positions);
            }
            else if constexpr (std::is_same_v<Request, LXSetNodeCollapsed>)
            {
                changed = graph_.SetNodeCollapsed(request.node, request.collapsed);
            }
            else if constexpr (std::is_same_v<Request, LXAddFrame>)
            {
                result.created = graph_.AddFrame(request.label, request.x, request.y, request.width, request.height,
                                                 request.members);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXRemoveFrame>)
            {
                changed = graph_.RemoveFrame(request.frame);
            }
            else if constexpr (std::is_same_v<Request, LXRenameFrame>)
            {
                changed = graph_.RenameFrame(request.frame, request.label);
            }
            else if constexpr (std::is_same_v<Request, LXMoveFrame>)
            {
                changed = graph_.MoveFrame(request.frame, request.x, request.y);
            }
            else if constexpr (std::is_same_v<Request, LXResizeFrame>)
            {
                changed = graph_.ResizeFrame(request.frame, request.width, request.height);
            }
            else if constexpr (std::is_same_v<Request, LXSetNodeFrame>)
            {
                changed = graph_.SetNodeFrame(request.node, request.frame);
            }
            else if constexpr (std::is_same_v<Request, LXSetView>)
            {
                changed = graph_.SetView(request.view);
            }
            else if constexpr (std::is_same_v<Request, LXAddDynamicPin>)
            {
                result.created = graph_.AddDynamicPin(request.node, request.name);
                changed = result.created != 0;
            }
            else if constexpr (std::is_same_v<Request, LXRemoveDynamicPin>)
            {
                changed = graph_.RemoveDynamicPin(request.pin);
            }
            else if constexpr (std::is_same_v<Request, LXMoveDynamicPin>)
            {
                changed = graph_.MoveDynamicPin(request.pin, request.direction);
            }
            else if constexpr (std::is_same_v<Request, LXUndo>)
            {
                changed = graph_.Undo();
            }
            else if constexpr (std::is_same_v<Request, LXRedo>)
            {
                changed = graph_.Redo();
            }
        },
        command);
    if (!changed)
    {
        result.code = "rejected_or_unchanged";
        return result;
    }
    ++revision_;
    dirty_ = unsavedInitial_ || !graph_.Equals(saved_);
    result.applied = true;
    result.code = "ok";
    result.revision = revision_;
    return result;
}

} // namespace LX
