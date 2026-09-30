#pragma once

#include "../Core/LXGraph.h"
#include "LXNodeItems.h"
#include "LXStyle.h"

#include <imgui.h>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace LX
{

class LXDocument;

struct CanvasState
{
    ImVec2 pan{25, 30};
    ImVec2 canvasSize{};
    float zoom = 1.0f;
    float viewDpi = 0.0f;
    bool viewApplied = false;
    bool panning = false;
    bool snapToGrid = false;
    Id selectedNode = 0;
    Id selectedFrame = 0;
    std::vector<Id> selectedNodes;
    Id pendingPin = 0;
    ImVec2 pendingStartMouse{};
    ImVec2 pendingAnchor{};
    Id creationPin = 0;
    ImVec2 creationAt{};
    bool creationPopupOpen = false;
    std::array<char, 64> creationQuery{};
    Id draggingNode = 0;
    ImVec2 dragStartMouse{};
    std::vector<NodePosition> dragStartNodes;
    Id draggingFrame = 0;
    ImVec2 frameDragStartMouse{};
    float frameDragStartX = 0.0f;
    float frameDragStartY = 0.0f;
    GraphFragment clipboard;
    unsigned pasteCount = 0;
    Id sliderNode = 0;
    std::string sliderKey;
    float sliderValue = 0.0f;
    Id colorNode = 0;
    std::string colorKey;
    ImVec4 colorValue{};
    Id editingPin = 0;
    std::array<float, 3> vectorValue{};
    std::int64_t integerValue = 0;
    std::string editingProperty;
    Id editingNode = 0;
    std::array<char, 512> textValue{};
    std::string message;
    bool dirty = false;
};

struct LXNodeGeometry
{
    float width = 0.0f;
    float height = 0.0f;
};

struct LXNodeItemRect
{
    ImVec2 minimum{};
    ImVec2 maximum{};
};

struct LXHeaderLinkEndpoints
{
    ImVec2 output{};
    ImVec2 input{};
};

// Uses the same user and monitor factors as ImGui font/widget sizes.
float CanvasUiScale();

LXNodeGeometry MeasureNode(const Node& node, const NodeLayout& layout, const LXNodeStyle& style,
                           const LXNodeItemRegistry& items);
ImVec2 PinPosition(const Node& node, const NodeLayout& layout, const Pin& pin, const LXNodeStyle& style,
                   const LXNodeItemRegistry* items = nullptr);
LXNodeItemRect ItemRect(const Node& node, const NodeLayout& layout, const std::string& key, const LXNodeStyle& style,
                        const LXNodeItemRegistry& items);
std::optional<LXHeaderLinkEndpoints> HeaderLinkEndpoints(const LXGraph& graph, const Link& link,
                                                         const LXStyleSheet& styles, const LXNodeItemRegistry& items);

// Draws inside the current ImGui window. The graph remains free of ImGui types.
void DrawCanvas(LXGraph& graph, CanvasState& state, const LXStyleSheet& styles, const LXNodeItemRegistry& items,
                LXDocument* document = nullptr);

} // namespace LX
