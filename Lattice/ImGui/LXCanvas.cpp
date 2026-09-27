#include "LXCanvas.h"
#include "../Core/LXDocument.h"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cfloat>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace LX
{
namespace
{
ImU32 Color(const ImVec4& value)
{
    return ImGui::ColorConvertFloat4ToU32(value);
}

ImVec2 Add(ImVec2 a, ImVec2 b)
{
    return {a.x + b.x, a.y + b.y};
}
ImVec2 Sub(ImVec2 a, ImVec2 b)
{
    return {a.x - b.x, a.y - b.y};
}
ImVec2 Mul(ImVec2 a, float b)
{
    return {a.x * b, a.y * b};
}
float DistanceSquared(ImVec2 a, ImVec2 b)
{
    const auto d = Sub(a, b);
    return d.x * d.x + d.y * d.y;
}
ImVec2 ToScreen(ImVec2 origin, const CanvasState& state, ImVec2 graphPoint, float scale)
{
    return Add(Add(origin, state.pan), Mul(graphPoint, scale));
}
const Node* Owner(const LXGraph& graph, Id pinId)
{
    const Pin* pin = graph.FindPin(pinId);
    return pin ? graph.FindNode(pin->node) : nullptr;
}

std::size_t RowCount(const Node& node)
{
    std::size_t inputs = 0;
    std::size_t outputs = 0;
    for (const Pin& pin : node.pins)
    {
        if (pin.direction == Direction::Input)
        {
            ++inputs;
        }
        else
        {
            ++outputs;
        }
    }
    return std::max(inputs, outputs);
}

float ItemHeight(const Node& node, const std::string& key, const LXNodeStyle& style, const LXNodeItemRegistry& items)
{
    const LXNodeItemSpec* item = items.Find(node, key);
    if (item && item->height > 0.0f)
    {
        return item->height;
    }
    if (item && item->kind == LXNodeItemKind::TexturePreview)
    {
        return std::max(style.propertyPreviewHeight, 72.0f);
    }
    return style.propertyPreviewHeight;
}

const Pin* ItemInputPin(const Node& node, const std::string& key, const LXNodeItemRegistry& items)
{
    const LXNodeItemSpec* item = items.Find(node, key);
    if (!item || item->inputPin.empty())
    {
        return nullptr;
    }
    const auto match = std::find_if(node.pins.begin(), node.pins.end(), [&](const Pin& pin) {
        return pin.direction == Direction::Input && pin.Identifier() == item->inputPin;
    });
    return match == node.pins.end() ? nullptr : &*match;
}

float ReadFloat(const std::string& value)
{
    float parsed = 0.0f;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || !std::isfinite(parsed))
    {
        return 0.0f;
    }
    return parsed;
}

std::string WriteFloat(float value)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

ImVec4 ReadColor(const std::string& value)
{
    if (value.size() != 7 || value.front() != '#')
    {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }
    ImVec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float* channels[] = {&color.x, &color.y, &color.z};
    for (std::size_t index = 0; index < 3; ++index)
    {
        unsigned component = 0;
        const char* first = value.data() + 1 + index * 2;
        const auto result = std::from_chars(first, first + 2, component, 16);
        if (result.ec != std::errc{} || result.ptr != first + 2)
        {
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
        *channels[index] = static_cast<float>(component) / 255.0f;
    }
    return color;
}

std::string WriteColor(const ImVec4& color)
{
    std::ostringstream stream;
    stream << '#' << std::uppercase << std::hex << std::setfill('0');
    for (const float channel : {color.x, color.y, color.z})
    {
        stream << std::setw(2) << static_cast<int>(std::round(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
    }
    return stream.str();
}

std::string ItemValue(const Node& node, const std::string& key, const LXNodeItemRegistry& items)
{
    const auto property = node.properties.find(key);
    const std::string fallback = property == node.properties.end() ? std::string{} : property->second;
    const Pin* pin = items.ValuePin(node, key);
    if (!pin || std::holds_alternative<std::monostate>(pin->value))
    {
        return fallback;
    }
    if (const auto* value = std::get_if<bool>(&pin->value))
    {
        return *value ? "True" : "False";
    }
    if (const auto* value = std::get_if<std::int64_t>(&pin->value))
    {
        return std::to_string(*value);
    }
    if (const auto* value = std::get_if<double>(&pin->value))
    {
        return WriteFloat(static_cast<float>(*value));
    }
    if (const auto* value = std::get_if<std::array<double, 4>>(&pin->value))
    {
        return WriteColor({static_cast<float>((*value)[0]), static_cast<float>((*value)[1]),
                           static_cast<float>((*value)[2]), static_cast<float>((*value)[3])});
    }
    if (const auto* value = std::get_if<std::array<double, 3>>(&pin->value))
    {
        std::ostringstream stream;
        stream << (*value)[0] << ", " << (*value)[1] << ", " << (*value)[2];
        return stream.str();
    }
    if (const auto* value = std::get_if<std::string>(&pin->value))
    {
        return *value;
    }
    return fallback;
}

ImVec4 ItemColor(const Node& node, const std::string& key, const LXNodeItemRegistry& items)
{
    const Pin* pin = items.ValuePin(node, key);
    if (pin)
    {
        if (const auto* value = std::get_if<std::array<double, 4>>(&pin->value))
        {
            return {static_cast<float>((*value)[0]), static_cast<float>((*value)[1]), static_cast<float>((*value)[2]),
                    static_cast<float>((*value)[3])};
        }
    }
    return ReadColor(ItemValue(node, key, items));
}

void Wire(ImDrawList* draw, ImVec2 a, ImVec2 b, const LXWireStyle& style, bool selected, float scale, bool vertical,
          bool followDirection = false)
{
    const float bend = std::max(style.bend * scale, std::abs((vertical ? b.y - a.y : b.x - a.x)) * 0.45f);
    ImVec2 first = vertical ? ImVec2{a.x, a.y + bend} : ImVec2{a.x + bend, a.y};
    ImVec2 second = vertical ? ImVec2{b.x, b.y - bend} : ImVec2{b.x - bend, b.y};
    if (followDirection)
    {
        const float direction = b.x >= a.x ? 1.0f : -1.0f;
        first = {a.x + bend * direction, a.y};
        second = {b.x - bend * direction, b.y};
    }
    const ImU32 color = Color(selected ? style.selectedColor : style.color);
    const float thickness = (selected ? style.selectedThickness : style.thickness) * scale;
    const bool directRoute = followDirection && style.route == LXWireStyle::Route::Straight;
    ImVec2 directStart{};
    ImVec2 directEnd{};
    if (directRoute)
    {
        const float direction = b.x >= a.x ? 1.0f : -1.0f;
        const float stub = std::min(style.bend * scale, std::abs(b.x - a.x) * 0.22f);
        directStart = {a.x + stub * direction, a.y};
        directEnd = {b.x - stub * direction, b.y};
        draw->PathClear();
        draw->PathLineTo(a);
        draw->PathLineTo(directStart);
        draw->PathLineTo(directEnd);
        draw->PathLineTo(b);
        draw->PathStroke(color, thickness);
    }
    else if (style.route == LXWireStyle::Route::Straight)
    {
        draw->AddLine(a, b, color, thickness);
    }
    else
    {
        draw->AddBezierCubic(a, first, second, b, color, thickness);
    }
    if (style.arrowAtMidpoint)
    {
        const ImVec2 midpoint = directRoute ? Mul(Add(directStart, directEnd), 0.5f)
                                : style.route == LXWireStyle::Route::Straight
                                    ? Mul(Add(a, b), 0.5f)
                                    : Mul(Add(Add(a, b), Mul(Add(first, second), 3.0f)), 0.125f);
        const ImVec2 tangent =
            directRoute ? Sub(directEnd, directStart)
            : style.route == LXWireStyle::Route::Straight
                ? Sub(b, a)
                : Add(Add(Mul(Sub(first, a), 0.75f), Mul(Sub(second, first), 1.5f)), Mul(Sub(b, second), 0.75f));
        const float length = std::sqrt(DistanceSquared(tangent, {0.0f, 0.0f}));
        if (length > 0.001f)
        {
            const ImVec2 direction = Mul(tangent, 1.0f / length);
            const ImVec2 normal{-direction.y, direction.x};
            const ImVec2 tip = Add(midpoint, Mul(direction, 7.0f * scale));
            const ImVec2 base = Sub(midpoint, Mul(direction, 5.0f * scale));
            draw->AddTriangleFilled(tip, Add(base, Mul(normal, 4.5f * scale)), Sub(base, Mul(normal, 4.5f * scale)),
                                    color);
        }
    }
    if (vertical)
    {
        const float arrow = 5.0f * scale;
        const ImVec2 tip{b.x, b.y - 6.0f * scale};
        draw->AddTriangleFilled(tip, {tip.x - arrow * 0.65f, tip.y - arrow}, {tip.x + arrow * 0.65f, tip.y - arrow},
                                color);
    }
}

ImVec2 CapsuleSideAtY(const NodeLayout& layout, LXNodeGeometry geometry, Direction side, float y)
{
    const float radius = std::min(geometry.width, geometry.height) * 0.5f;
    const float offset = y - layout.y - geometry.height * 0.5f;
    const float inset = radius - std::sqrt(std::max(0.0f, radius * radius - offset * offset));
    const float x = side == Direction::Output ? layout.x + geometry.width - inset : layout.x + inset;
    return {x, y};
}

ImVec2 CapsuleSidePort(const NodeLayout& layout, LXNodeGeometry geometry, Direction side, std::size_t index,
                       std::size_t count)
{
    const float limit = std::min(geometry.width, geometry.height) * 0.4f;
    const float spacing = count > 1 ? std::min(14.0f, limit * 2.0f / static_cast<float>(count - 1)) : 0.0f;
    const float offset = (static_cast<float>(index) - static_cast<float>(count - 1) * 0.5f) * spacing;
    return CapsuleSideAtY(layout, geometry, side, layout.y + geometry.height * 0.5f + offset);
}

void DrawPin(ImDrawList* draw, ImVec2 center, Direction direction, const LXPinStyle& style, float scale, bool active,
             bool vertical)
{
    if (!style.visible)
    {
        return;
    }
    const float radius = style.radius * scale * (active ? 1.25f : 1.0f);
    const ImU32 fill = Color(style.fill);
    const ImU32 border = Color(style.border);
    const float borderThickness = style.borderThickness * scale;

    switch (style.shape)
    {
    case LXPinShape::Circle:
        draw->AddCircleFilled(center, radius, fill);
        draw->AddCircle(center, radius, border, 0, borderThickness);
        break;
    case LXPinShape::Triangle: {
        const float directionSign = direction == Direction::Output ? 1.0f : -1.0f;
        const ImVec2 tip = vertical ? ImVec2{center.x, center.y + directionSign * radius}
                                    : ImVec2{center.x + directionSign * radius, center.y};
        const ImVec2 top = vertical ? ImVec2{center.x - radius, center.y - directionSign * radius}
                                    : ImVec2{center.x - directionSign * radius, center.y - radius};
        const ImVec2 bottom = vertical ? ImVec2{center.x + radius, center.y - directionSign * radius}
                                       : ImVec2{center.x - directionSign * radius, center.y + radius};
        draw->AddTriangleFilled(tip, top, bottom, fill);
        draw->AddTriangle(tip, top, bottom, border, borderThickness);
        break;
    }
    case LXPinShape::Diamond: {
        const ImVec2 left{center.x - radius, center.y};
        const ImVec2 top{center.x, center.y - radius};
        const ImVec2 right{center.x + radius, center.y};
        const ImVec2 bottom{center.x, center.y + radius};
        draw->AddQuadFilled(left, top, right, bottom, fill);
        draw->AddQuad(left, top, right, bottom, border, borderThickness);
        break;
    }
    }
}

void DrawGrid(ImDrawList* draw, ImVec2 origin, ImVec2 size, const CanvasState& state, const LXCanvasStyle& style,
              float scale)
{
    if (!style.showGrid || style.gridSpacing <= 0.0f)
    {
        return;
    }

    const float spacing = style.gridSpacing * scale;
    const ImU32 color = Color(style.grid);
    for (float x = origin.x + std::fmod(state.pan.x, spacing); x < origin.x + size.x; x += spacing)
    {
        draw->AddLine({x, origin.y}, {x, origin.y + size.y}, color);
    }
    for (float y = origin.y + std::fmod(state.pan.y, spacing); y < origin.y + size.y; y += spacing)
    {
        draw->AddLine({origin.x, y}, {origin.x + size.x, y}, color);
    }
}

void DrawTexturePreview(ImDrawList* draw, ImVec2 minimum, ImVec2 maximum, const std::string& assetId,
                        ImTextureID texture, float textSize)
{
    draw->PushClipRect(minimum, maximum, true);
    if (texture != ImTextureID_Invalid)
    {
        draw->AddImage(ImTextureRef(texture), minimum, maximum);
    }
    else
    {
        // A distinct illustrative thumbnail is used until a domain resolves a real texture.
        const bool normal = assetId.find("Normal") != std::string::npos;
        const ImVec4 dark = normal ? ImVec4{0.22f, 0.28f, 0.60f, 1.0f} : ImVec4{0.19f, 0.12f, 0.08f, 1.0f};
        const ImVec4 light = normal ? ImVec4{0.55f, 0.61f, 0.94f, 1.0f} : ImVec4{0.62f, 0.42f, 0.26f, 1.0f};
        draw->AddRectFilledMultiColor(minimum, maximum, Color(dark), Color(light), Color(dark), Color(light));
        for (int stripe = 0; stripe < 8; ++stripe)
        {
            const float x = minimum.x + (maximum.x - minimum.x) * static_cast<float>(stripe) / 8.0f;
            const float offset = stripe % 2 == 0 ? 7.0f : -5.0f;
            draw->AddLine({x, minimum.y}, {x + offset, maximum.y},
                          Color(normal ? ImVec4{0.75f, 0.78f, 1.0f, 0.35f} : ImVec4{0.90f, 0.68f, 0.43f, 0.35f}), 2.0f);
        }
    }
    const float captionHeight = textSize + 6.0f;
    draw->AddRectFilled({minimum.x, maximum.y - captionHeight}, maximum, IM_COL32(10, 14, 17, 210));
    const ImVec4 labelClip{minimum.x + 5.0f, maximum.y - captionHeight, maximum.x - 5.0f, maximum.y};
    draw->AddText(ImGui::GetFont(), textSize, {minimum.x + 5.0f, maximum.y - captionHeight + 2.0f},
                  IM_COL32(240, 244, 246, 255), assetId.c_str(), nullptr, 0.0f, &labelClip);
    draw->PopClipRect();
}
} // namespace

std::optional<LXHeaderLinkEndpoints> HeaderLinkEndpoints(const LXGraph& graph, const Link& link,
                                                         const LXStyleSheet& styles, const LXNodeItemRegistry& items)
{
    const Pin* output = graph.FindPin(link.output);
    const Pin* input = graph.FindPin(link.input);
    const Node* source = output ? graph.FindNode(output->node) : nullptr;
    const Node* target = input ? graph.FindNode(input->node) : nullptr;
    const NodeLayout* sourceLayout = source ? graph.FindLayout(source->id) : nullptr;
    const NodeLayout* targetLayout = target ? graph.FindLayout(target->id) : nullptr;
    if (!source || !target || !sourceLayout || !targetLayout || !styles.ForNode(*source).headerOnly ||
        !styles.ForNode(*target).headerOnly)
    {
        return std::nullopt;
    }

    const auto face = [&](const Node& node, const Node& other, bool outgoing) {
        const NodeLayout& nodeLayout = *graph.FindLayout(node.id);
        const NodeLayout& otherLayout = *graph.FindLayout(other.id);
        const LXNodeGeometry nodeSize = MeasureNode(node, nodeLayout, styles.ForNode(node), items);
        const LXNodeGeometry otherSize = MeasureNode(other, otherLayout, styles.ForNode(other), items);
        const float nodeCenterX = nodeLayout.x + nodeSize.width * 0.5f;
        const float otherCenterX = otherLayout.x + otherSize.width * 0.5f;
        return otherCenterX > nodeCenterX || (otherCenterX == nodeCenterX && outgoing) ? Direction::Output
                                                                                       : Direction::Input;
    };
    const Direction outputSide = face(*source, *target, true);
    const Direction inputSide = face(*target, *source, false);
    const auto order = [&](Id nodeId, Direction side) {
        std::size_t index = 0;
        std::size_t count = 0;
        for (const Link& candidate : graph.Links())
        {
            const Pin* candidateOutput = graph.FindPin(candidate.output);
            const Pin* candidateInput = graph.FindPin(candidate.input);
            const Node* candidateSource = candidateOutput ? graph.FindNode(candidateOutput->node) : nullptr;
            const Node* candidateTarget = candidateInput ? graph.FindNode(candidateInput->node) : nullptr;
            if (!candidateSource || !candidateTarget || !graph.FindLayout(candidateSource->id) ||
                !graph.FindLayout(candidateTarget->id) || !styles.ForNode(*candidateSource).headerOnly ||
                !styles.ForNode(*candidateTarget).headerOnly)
            {
                continue;
            }
            const bool onSource =
                candidateSource->id == nodeId && face(*candidateSource, *candidateTarget, true) == side;
            const bool onTarget =
                candidateTarget->id == nodeId && face(*candidateTarget, *candidateSource, false) == side;
            if (onSource || onTarget)
            {
                if (candidate.id == link.id)
                {
                    index = count;
                }
                ++count;
            }
        }
        return std::pair{index, count};
    };
    const auto [outputIndex, outputCount] = order(source->id, outputSide);
    const auto [inputIndex, inputCount] = order(target->id, inputSide);
    if (!outputCount || !inputCount)
    {
        return std::nullopt;
    }
    const LXNodeGeometry sourceSize = MeasureNode(*source, *sourceLayout, styles.ForNode(*source), items);
    const LXNodeGeometry targetSize = MeasureNode(*target, *targetLayout, styles.ForNode(*target), items);
    LXHeaderLinkEndpoints endpoints{CapsuleSidePort(*sourceLayout, sourceSize, outputSide, outputIndex, outputCount),
                                    CapsuleSidePort(*targetLayout, targetSize, inputSide, inputIndex, inputCount)};
    const float straightTolerance = std::min(12.0f, std::min(sourceSize.height, targetSize.height) * 0.3f);
    if (std::abs(endpoints.output.y - endpoints.input.y) <= straightTolerance)
    {
        const float alignedY = (endpoints.output.y + endpoints.input.y) * 0.5f;
        const float sourceLimit = std::min(sourceSize.width, sourceSize.height) * 0.4f;
        const float targetLimit = std::min(targetSize.width, targetSize.height) * 0.4f;
        const float sourceCenterY = sourceLayout->y + sourceSize.height * 0.5f;
        const float targetCenterY = targetLayout->y + targetSize.height * 0.5f;
        if (std::abs(alignedY - sourceCenterY) <= sourceLimit && std::abs(alignedY - targetCenterY) <= targetLimit)
        {
            endpoints.output = CapsuleSideAtY(*sourceLayout, sourceSize, outputSide, alignedY);
            endpoints.input = CapsuleSideAtY(*targetLayout, targetSize, inputSide, alignedY);
        }
    }
    return endpoints;
}

LXNodeGeometry MeasureNode(const Node& node, const NodeLayout& layout, const LXNodeStyle& style,
                           const LXNodeItemRegistry& items)
{
    if (layout.collapsed || style.headerOnly)
    {
        std::size_t inputs = 0;
        std::size_t outputs = 0;
        for (const Pin& pin : node.pins)
        {
            (pin.direction == Direction::Input ? inputs : outputs)++;
        }
        const float titleWidth = static_cast<float>(node.title.size()) * style.fontSize * 0.65f + 42.0f;
        const float width = std::min(style.width, std::max(90.0f, titleWidth));
        const float height =
            std::max(style.headerHeight + 4.0f, static_cast<float>(std::max(inputs, outputs)) * 12.0f + 10.0f);
        return {width, height};
    }
    const float rows = style.pinLayout == LXPinLayout::TopBottom ? 0.0f : static_cast<float>(RowCount(node));
    float contentHeight = style.headerHeight + rows * style.rowHeight + style.bodyBottomPadding;
    if (style.showPropertyPreview)
    {
        bool hasUnanchoredItems = false;
        for (const std::string& key : items.Keys(node))
        {
            if (!ItemInputPin(node, key, items))
            {
                contentHeight += ItemHeight(node, key, style, items) + style.propertyMargin;
                hasUnanchoredItems = true;
            }
        }
        if (hasUnanchoredItems)
        {
            contentHeight += style.propertyMargin;
        }
    }
    return {style.width, std::max(style.height, contentHeight)};
}

LXNodeItemRect ItemRect(const Node& node, const NodeLayout& layout, const std::string& key, const LXNodeStyle& style,
                        const LXNodeItemRegistry& items)
{
    if (!style.showPropertyPreview || layout.collapsed || style.headerOnly)
    {
        return {};
    }

    if (const Pin* pin = ItemInputPin(node, key, items))
    {
        const float height = std::max(1.0f, std::min(ItemHeight(node, key, style, items), style.rowHeight - 2.0f));
        const ImVec2 point = PinPosition(node, layout, *pin, style, &items);
        return {{layout.x + style.propertyMargin, point.y - height * 0.5f},
                {layout.x + style.width - style.propertyMargin, point.y + height * 0.5f}};
    }

    const float rows = style.pinLayout == LXPinLayout::TopBottom ? 0.0f : static_cast<float>(RowCount(node));
    float y = layout.y + style.headerHeight + rows * style.rowHeight + style.propertyMargin;
    for (const std::string& candidate : items.Keys(node))
    {
        if (ItemInputPin(node, candidate, items))
        {
            continue;
        }
        const float height = ItemHeight(node, candidate, style, items);
        if (candidate == key)
        {
            return {{layout.x + style.propertyMargin, y}, {layout.x + style.width - style.propertyMargin, y + height}};
        }
        y += height + style.propertyMargin;
    }
    return {};
}

ImVec2 PinPosition(const Node& node, const NodeLayout& layout, const Pin& pin, const LXNodeStyle& style,
                   const LXNodeItemRegistry* items)
{
    std::size_t index = 0;
    for (const Pin& candidate : node.pins)
    {
        if (candidate.direction != pin.direction)
        {
            continue;
        }
        if (candidate.id == pin.id)
        {
            break;
        }
        ++index;
    }
    const std::size_t directionCount =
        static_cast<std::size_t>(std::count_if(node.pins.begin(), node.pins.end(), [&](const Pin& candidate) {
            return candidate.direction == pin.direction;
        }));
    if (style.pinLayout == LXPinLayout::TopBottom)
    {
        const LXNodeItemRegistry emptyItems;
        const LXNodeGeometry geometry = MeasureNode(node, layout, style, items ? *items : emptyItems);
        const float x = layout.x + geometry.width * (static_cast<float>(index) + 1.0f) /
                                       (static_cast<float>(directionCount) + 1.0f);
        const float y = layout.y + (pin.direction == Direction::Output ? geometry.height : 0.0f);
        return {x, y};
    }
    const LXNodeGeometry geometry = MeasureNode(node, layout, style, items ? *items : LXNodeItemRegistry{});
    const float x = layout.x + (pin.direction == Direction::Output ? geometry.width : 0.0f);
    if (layout.collapsed || style.headerOnly)
    {
        return {x, layout.y + geometry.height * (static_cast<float>(index) + 1.0f) /
                                  (static_cast<float>(directionCount) + 1.0f)};
    }
    const float y = layout.y + style.headerHeight + (static_cast<float>(index) + 0.5f) * style.rowHeight;
    return {x, y};
}

void DrawCanvas(LXGraph& graph, CanvasState& state, const LXStyleSheet& styles, const LXNodeItemRegistry& items,
                LXDocument* document)
{
    assert(!document || &graph == &document->GraphForCanvas());
    const auto connect = [&](Id first, Id second, std::string* reason) -> std::optional<Id> {
        if (!document)
        {
            return graph.Connect(first, second, reason);
        }
        const LXCommandResult result = document->Execute(LXConnectPins{first, second}, document->Revision());
        if (reason)
        {
            *reason = result.message;
        }
        return result.applied ? std::optional<Id>{result.created} : std::nullopt;
    };
    const auto collapse = [&](Id node, bool value) {
        return document ? document->Execute(LXSetNodeCollapsed{node, value}, document->Revision()).applied
                        : graph.SetNodeCollapsed(node, value);
    };
    const auto previewPositions = [&](const std::vector<NodePosition>& positions) {
        return document ? document->PreviewNodePositions(positions) : graph.SetNodePositions(positions, false);
    };
    const auto finishPositions = [&](const std::vector<NodePosition>& start, const std::vector<NodePosition>& final) {
        if (document)
        {
            return document->FinishNodePositionPreview(start, final).applied;
        }
        graph.SetNodePositions(start, false);
        return graph.SetNodePositions(final, true);
    };
    const auto paste = [&](const GraphFragment& fragment, float x, float y) {
        return document ? document->Execute(LXPasteNodes{fragment, x, y}, document->Revision()).createdNodes
                        : graph.PasteNodes(fragment, x, y);
    };
    const auto remove = [&](const std::vector<Id>& nodes) {
        return document ? document->Execute(LXRemoveNodes{nodes}, document->Revision()).applied
                        : graph.RemoveNodes(nodes);
    };
    const auto undo = [&]() {
        return document ? document->Execute(LXUndo{}, document->Revision()).applied : graph.Undo();
    };
    const auto redo = [&]() {
        return document ? document->Execute(LXRedo{}, document->Revision()).applied : graph.Redo();
    };
    const auto setProperty = [&](Id node, const std::string& key, const std::string& value) {
        return document ? document->Execute(LXSetProperty{node, key, value}, document->Revision()).applied
                        : graph.SetProperty(node, key, value);
    };
    const auto setSocketValue = [&](Id pin, LXSocketValue value) {
        return document ? document->Execute(LXSetSocketValue{pin, std::move(value)}, document->Revision()).applied
                        : graph.SetSocketValue(pin, std::move(value));
    };
    const float dpi = ImGui::GetStyle().FontScaleDpi;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    if (document && state.viewApplied && state.canvasSize.x > 0.0f && state.canvasSize.y > 0.0f &&
        state.viewDpi > 0.0f && (state.canvasSize.x != size.x || state.canvasSize.y != size.y || state.viewDpi != dpi))
    {
        const float centerX = (state.canvasSize.x * 0.5f - state.pan.x) / (state.zoom * state.viewDpi);
        const float centerY = (state.canvasSize.y * 0.5f - state.pan.y) / (state.zoom * state.viewDpi);
        state.pan = {size.x * 0.5f - centerX * state.zoom * dpi, size.y * 0.5f - centerY * state.zoom * dpi};
    }
    state.canvasSize = size;
    state.viewDpi = dpi;
    if (document && !state.viewApplied)
    {
        const ViewLayout& view = graph.Layout().view;
        if (view.saved)
        {
            state.zoom = view.zoom;
            state.pan = {size.x * 0.5f - view.centerX * state.zoom * dpi,
                         size.y * 0.5f - view.centerY * state.zoom * dpi};
        }
        state.viewApplied = true;
    }
    const float scale = state.zoom * dpi;
    const auto saveView = [&]() {
        if (!document)
        {
            return;
        }
        const ViewLayout view{true, (size.x * 0.5f - state.pan.x) / (state.zoom * dpi),
                              (size.y * 0.5f - state.pan.y) / (state.zoom * dpi), state.zoom};
        if (document->Execute(LXSetView{view}, document->Revision()).applied)
        {
            state.dirty = document->Dirty();
        }
    };
    ImGui::Dummy(size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 cursorAfterCanvas = ImGui::GetCursorScreenPos();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(origin, Add(origin, size), true);
    draw->AddRectFilled(origin, Add(origin, size), Color(styles.canvas.background));
    DrawGrid(draw, origin, size, state, styles.canvas, scale);
    for (const auto& [id, frame] : graph.Layout().frames)
    {
        const ImVec2 top = ToScreen(origin, state, {frame.x, frame.y}, scale);
        const ImVec2 bottom = Add(top, {frame.width * scale, frame.height * scale});
        draw->AddRectFilled(top, bottom, Color(styles.frame.fill), styles.frame.rounding * scale);
        draw->AddRectFilled(top, {bottom.x, top.y + styles.frame.headerHeight * scale}, Color(styles.frame.header),
                            styles.frame.rounding * scale, ImDrawFlags_RoundCornersTop);
        draw->AddRect(
            top, bottom, Color(id == state.selectedFrame ? styles.frame.selectedBorder : styles.frame.border),
            styles.frame.rounding * scale, 0,
            (id == state.selectedFrame ? styles.frame.selectedBorderThickness : styles.frame.borderThickness) * scale);
        draw->AddText({top.x + 8.0f * scale, top.y + 5.0f * scale}, Color(styles.frame.text), frame.label.c_str());
    }
    const auto selected = [&](Id id) {
        return state.selectedNode == id ||
               std::find(state.selectedNodes.begin(), state.selectedNodes.end(), id) != state.selectedNodes.end();
    };

    if (hovered && ImGui::GetIO().MouseWheel != 0)
    {
        const float old = state.zoom;
        state.zoom = std::clamp(old * std::pow(styles.canvas.zoomStep, ImGui::GetIO().MouseWheel),
                                styles.canvas.minZoom, styles.canvas.maxZoom);
        state.pan = Sub(Sub(mouse, origin), Mul(Sub(Sub(mouse, origin), state.pan), state.zoom / old));
        saveView();
    }
    if (hovered &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0)))
    {
        state.pan = Add(state.pan, ImGui::GetIO().MouseDelta);
        state.panning = true;
    }
    if (state.panning &&
        (ImGui::IsMouseReleased(ImGuiMouseButton_Middle) || ImGui::IsMouseReleased(ImGuiMouseButton_Right)))
    {
        saveView();
        state.panning = false;
    }

    const auto hasHeaderConnectionOnDefaultSide = [&](const Pin& pin) {
        const Node* owner = graph.FindNode(pin.node);
        const NodeLayout* ownerLayout = owner ? graph.FindLayout(owner->id) : nullptr;
        if (!owner || !ownerLayout)
        {
            return false;
        }
        const float ownerCenterX =
            ownerLayout->x + MeasureNode(*owner, *ownerLayout, styles.ForNode(*owner), items).width * 0.5f;
        return std::any_of(graph.Links().begin(), graph.Links().end(), [&](const Link& link) {
            if (link.output != pin.id && link.input != pin.id)
            {
                return false;
            }
            const Node* source = Owner(graph, link.output);
            const Node* target = Owner(graph, link.input);
            const Node* other = link.output == pin.id ? target : source;
            const NodeLayout* otherLayout = other ? graph.FindLayout(other->id) : nullptr;
            if (!source || !target || !otherLayout || !styles.ForNode(*source).headerOnly ||
                !styles.ForNode(*target).headerOnly)
            {
                return false;
            }
            const float otherCenterX =
                otherLayout->x + MeasureNode(*other, *otherLayout, styles.ForNode(*other), items).width * 0.5f;
            const bool facesRight =
                otherCenterX > ownerCenterX || (otherCenterX == ownerCenterX && pin.direction == Direction::Output);
            return facesRight == (pin.direction == Direction::Output);
        });
    };
    const auto forEachHeaderPort = [&](auto&& visit) {
        for (const Link& link : graph.Links())
        {
            const auto endpoints = HeaderLinkEndpoints(graph, link, styles, items);
            if (!endpoints)
            {
                continue;
            }
            if (const Pin* output = graph.FindPin(link.output))
            {
                visit(*output, endpoints->output);
            }
            if (const Pin* input = graph.FindPin(link.input))
            {
                visit(*input, endpoints->input);
            }
        }
    };

    for (const Link& link : graph.Links())
    {
        const Pin* output = graph.FindPin(link.output);
        const Pin* input = graph.FindPin(link.input);
        const Node* source = Owner(graph, link.output);
        const Node* target = Owner(graph, link.input);
        const NodeLayout* sourceLayout = source ? graph.FindLayout(source->id) : nullptr;
        const NodeLayout* targetLayout = target ? graph.FindLayout(target->id) : nullptr;
        if (output && input && source && target && sourceLayout && targetLayout)
        {
            const bool linkSelected = selected(source->id) || selected(target->id);
            const LXNodeStyle& sourceStyle = styles.ForNode(*source);
            const LXNodeStyle& targetStyle = styles.ForNode(*target);
            if (sourceStyle.headerOnly && targetStyle.headerOnly)
            {
                if (const auto endpoints = HeaderLinkEndpoints(graph, link, styles, items))
                {
                    Wire(draw, ToScreen(origin, state, endpoints->output, scale),
                         ToScreen(origin, state, endpoints->input, scale), styles.ForWire(link, output->type),
                         linkSelected, scale, false, true);
                }
            }
            else
            {
                Wire(draw,
                     ToScreen(origin, state, PinPosition(*source, *sourceLayout, *output, sourceStyle, &items), scale),
                     ToScreen(origin, state, PinPosition(*target, *targetLayout, *input, targetStyle, &items), scale),
                     styles.ForWire(link, output->type), linkSelected, scale,
                     sourceStyle.pinLayout == LXPinLayout::TopBottom &&
                         targetStyle.pinLayout == LXPinLayout::TopBottom);
            }
        }
    }

    Id hoveredPin = 0, hoveredNode = 0, hoveredCollapse = 0, hoveredFrame = 0;
    ImVec2 hoveredPinPosition{};
    float hoveredPinDistance = std::numeric_limits<float>::max();
    bool hoveredWidget = false;
    for (const Node& node : graph.Nodes())
    {
        const NodeLayout& layout = *graph.FindLayout(node.id);
        const LXNodeStyle& style = styles.ForNode(node);
        const LXNodeGeometry geometry = MeasureNode(node, layout, style, items);
        const ImVec2 top = ToScreen(origin, state, {layout.x, layout.y}, scale);
        const ImVec2 bottom = Add(top, Mul({geometry.width, geometry.height}, scale));
        if (mouse.x >= top.x && mouse.x <= bottom.x && mouse.y >= top.y && mouse.y <= bottom.y)
        {
            hoveredNode = node.id;
            const float headerHitHeight = layout.collapsed || style.headerOnly ? geometry.height : style.headerHeight;
            if (!style.headerOnly && mouse.x <= top.x + 21.0f * scale && mouse.y <= top.y + headerHitHeight * scale)
            {
                hoveredCollapse = node.id;
            }
            if (!layout.collapsed && !style.headerOnly)
            {
                for (const std::string& key : items.Keys(node))
                {
                    const LXNodeItemRect rect = ItemRect(node, layout, key, style, items);
                    const ImVec2 itemTop = ToScreen(origin, state, rect.minimum, scale);
                    const ImVec2 itemBottom = ToScreen(origin, state, rect.maximum, scale);
                    if (mouse.x >= itemTop.x && mouse.x <= itemBottom.x && mouse.y >= itemTop.y &&
                        mouse.y <= itemBottom.y)
                    {
                        hoveredWidget = true;
                        break;
                    }
                }
            }
        }
        for (const Pin& pin : node.pins)
        {
            if (style.headerOnly && hasHeaderConnectionOnDefaultSide(pin))
            {
                continue;
            }
            const ImVec2 position = PinPosition(node, layout, pin, style, &items);
            const ImVec2 point = ToScreen(origin, state, position, scale);
            const LXPinStyle& pinStyle = styles.ForPin(pin);
            const float hitRadius = (pinStyle.radius + pinStyle.hitPadding) * scale;
            const float distance = DistanceSquared(point, mouse);
            if (distance <= hitRadius * hitRadius && distance < hoveredPinDistance)
            {
                hoveredPin = pin.id;
                hoveredPinPosition = position;
                hoveredPinDistance = distance;
            }
        }
    }
    forEachHeaderPort([&](const Pin& pin, ImVec2 position) {
        const ImVec2 point = ToScreen(origin, state, position, scale);
        const LXPinStyle& pinStyle = styles.ForPin(pin);
        const float hitRadius = (pinStyle.radius + pinStyle.hitPadding) * scale;
        const float distance = DistanceSquared(point, mouse);
        if (distance <= hitRadius * hitRadius && distance < hoveredPinDistance)
        {
            hoveredPin = pin.id;
            hoveredPinPosition = position;
            hoveredPinDistance = distance;
        }
    });
    if (!hoveredNode && !hoveredPin)
    {
        for (auto frame = graph.Layout().frames.rbegin(); frame != graph.Layout().frames.rend(); ++frame)
        {
            const ImVec2 top = ToScreen(origin, state, {frame->second.x, frame->second.y}, scale);
            if (mouse.x >= top.x && mouse.x <= top.x + frame->second.width * scale && mouse.y >= top.y &&
                mouse.y <= top.y + styles.frame.headerHeight * scale)
            {
                hoveredFrame = frame->first;
                break;
            }
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hoveredPin)
        {
            state.selectedFrame = 0;
            if (state.pendingPin && state.pendingPin != hoveredPin)
            {
                std::string reason;
                if (connect(state.pendingPin, hoveredPin, &reason))
                {
                    state.dirty = true;
                    state.message = "Link created";
                }
                else
                {
                    state.message = reason;
                }
                state.pendingPin = 0;
            }
            else
            {
                state.pendingPin = hoveredPin;
                state.pendingStartMouse = mouse;
                state.pendingAnchor = hoveredPinPosition;
                state.message = "Select a compatible port";
            }
        }
        else if (hoveredCollapse)
        {
            state.selectedFrame = 0;
            const Node* node = graph.FindNode(hoveredCollapse);
            const NodeLayout* layout = node ? graph.FindLayout(node->id) : nullptr;
            if (node && layout && collapse(node->id, !layout->collapsed))
            {
                state.selectedNode = node->id;
                state.dirty = true;
            }
        }
        else if (hoveredNode)
        {
            state.selectedFrame = 0;
            if (ImGui::GetIO().KeyCtrl)
            {
                if (state.selectedNodes.empty() && state.selectedNode)
                {
                    state.selectedNodes.push_back(state.selectedNode);
                }
                const auto it = std::find(state.selectedNodes.begin(), state.selectedNodes.end(), hoveredNode);
                if (it == state.selectedNodes.end())
                {
                    state.selectedNodes.push_back(hoveredNode);
                    state.selectedNode = hoveredNode;
                }
                else
                {
                    state.selectedNodes.erase(it);
                    state.selectedNode = state.selectedNodes.empty() ? 0 : state.selectedNodes.back();
                }
            }
            else
            {
                if (std::find(state.selectedNodes.begin(), state.selectedNodes.end(), hoveredNode) ==
                    state.selectedNodes.end())
                {
                    state.selectedNodes = {hoveredNode};
                }
                state.selectedNode = hoveredNode;
                if (!hoveredWidget)
                {
                    state.draggingNode = hoveredNode;
                    state.dragStartMouse = mouse;
                    state.dragStartNodes.clear();
                    for (const Id id : state.selectedNodes)
                    {
                        if (const NodeLayout* layout = graph.FindLayout(id))
                        {
                            state.dragStartNodes.push_back({id, layout->x, layout->y});
                        }
                    }
                }
            }
        }
        else if (hoveredFrame)
        {
            const FrameLayout* frame = graph.FindFrame(hoveredFrame);
            state.selectedFrame = hoveredFrame;
            state.selectedNode = 0;
            state.selectedNodes.clear();
            state.draggingFrame = hoveredFrame;
            state.frameDragStartMouse = mouse;
            state.frameDragStartX = frame->x;
            state.frameDragStartY = frame->y;
        }
        else
        {
            state.selectedFrame = 0;
            state.selectedNode = 0;
            state.selectedNodes.clear();
            if (state.pendingPin)
            {
                state.creationPin = state.pendingPin;
                state.creationAt = Mul(Sub(Sub(mouse, origin), state.pan), 1.0f / scale);
            }
            state.pendingPin = 0;
        }
    }
    if (state.pendingPin && hoveredPin && hoveredPin != state.pendingPin &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        std::string reason;
        if (connect(state.pendingPin, hoveredPin, &reason))
        {
            state.dirty = true;
            state.message = "Link created";
        }
        else
        {
            state.message = reason;
        }
        state.pendingPin = 0;
    }
    if (state.pendingPin && hovered && !hoveredPin && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
        DistanceSquared(mouse, state.pendingStartMouse) > 36.0f * scale * scale)
    {
        state.creationPin = state.pendingPin;
        state.creationAt = Mul(Sub(Sub(mouse, origin), state.pan), 1.0f / scale);
        state.pendingPin = 0;
    }
    if (state.draggingNode && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 delta = Mul(Sub(mouse, state.dragStartMouse), 1.0f / scale);
        std::vector<NodePosition> positions = state.dragStartNodes;
        for (NodePosition& position : positions)
        {
            position.x += delta.x;
            position.y += delta.y;
        }
        if (previewPositions(positions))
        {
            state.dirty = true;
        }
    }
    if (state.draggingNode && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        std::vector<NodePosition> finalPositions;
        finalPositions.reserve(state.dragStartNodes.size());
        for (const NodePosition& position : state.dragStartNodes)
        {
            if (const NodeLayout* layout = graph.FindLayout(position.id))
            {
                finalPositions.push_back({position.id, layout->x, layout->y});
            }
        }
        finishPositions(state.dragStartNodes, finalPositions);
        state.draggingNode = 0;
        state.dragStartNodes.clear();
    }
    if (state.draggingFrame && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const ImVec2 delta = Mul(Sub(mouse, state.frameDragStartMouse), 1.0f / scale);
        const float x = state.frameDragStartX + delta.x;
        const float y = state.frameDragStartY + delta.y;
        const bool moved = document ? document->PreviewFramePosition(state.draggingFrame, x, y)
                                    : graph.MoveFrame(state.draggingFrame, x, y, false);
        if (moved)
        {
            state.dirty = true;
        }
    }
    if (state.draggingFrame && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        const FrameLayout* frame = graph.FindFrame(state.draggingFrame);
        if (frame)
        {
            const float x = frame->x;
            const float y = frame->y;
            if (document)
            {
                document->FinishFramePositionPreview(state.draggingFrame, state.frameDragStartX, state.frameDragStartY,
                                                     x, y);
            }
            else
            {
                graph.MoveFrame(state.draggingFrame, state.frameDragStartX, state.frameDragStartY, false);
                graph.MoveFrame(state.draggingFrame, x, y);
            }
        }
        state.draggingFrame = 0;
    }
    if (hovered && !ImGui::IsAnyItemActive())
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && state.selectedNode)
        {
            state.clipboard = graph.CopyNodes(state.selectedNodes.empty() ? std::vector<Id>{state.selectedNode}
                                                                          : state.selectedNodes);
            state.pasteCount = 0;
            state.message = "Selection copied";
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !state.clipboard.nodes.empty())
        {
            ++state.pasteCount;
            const float offset = 30.0f * state.pasteCount;
            state.selectedNodes = paste(state.clipboard, offset, offset);
            state.selectedNode = state.selectedNodes.empty() ? 0 : state.selectedNodes.back();
            state.dirty = !state.selectedNodes.empty();
            state.message = state.dirty ? "Selection pasted" : "Paste failed";
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && state.selectedNode &&
            remove(state.selectedNodes.empty() ? std::vector<Id>{state.selectedNode} : state.selectedNodes))
        {
            state.selectedNode = 0;
            state.selectedNodes.clear();
            state.dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && state.selectedFrame)
        {
            const bool removed =
                document ? document->Execute(LXRemoveFrame{state.selectedFrame}, document->Revision()).applied
                         : graph.RemoveFrame(state.selectedFrame);
            if (removed)
            {
                state.selectedFrame = 0;
                state.dirty = true;
            }
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z) && undo())
        {
            if (!graph.FindFrame(state.selectedFrame))
            {
                state.selectedFrame = 0;
            }
            state.selectedNodes.clear();
            if (!graph.FindNode(state.selectedNode))
            {
                state.selectedNode = 0;
            }
            state.dirty = true;
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y) && redo())
        {
            if (!graph.FindFrame(state.selectedFrame))
            {
                state.selectedFrame = 0;
            }
            state.selectedNodes.clear();
            if (!graph.FindNode(state.selectedNode))
            {
                state.selectedNode = 0;
            }
            state.dirty = true;
        }
    }

    for (const Node& node : graph.Nodes())
    {
        const NodeLayout& layout = *graph.FindLayout(node.id);
        const LXNodeStyle& style = styles.ForNode(node);
        const LXNodeGeometry geometry = MeasureNode(node, layout, style, items);
        const ImVec2 top = ToScreen(origin, state, {layout.x, layout.y}, scale);
        const ImVec2 bottom = Add(top, Mul({geometry.width, geometry.height}, scale));
        const bool nodeSelected = selected(node.id);
        const ImVec2 shadowOffset{2.0f * scale, styles.canvas.shadowOffset * scale};
        const bool compact = layout.collapsed || style.headerOnly;
        const float rounding = compact ? geometry.height * 0.5f : style.rounding;
        draw->AddRectFilled(Add(top, shadowOffset), Add(bottom, shadowOffset), Color(styles.canvas.shadow),
                            rounding * scale);
        draw->AddRectFilled(top, bottom, Color(compact ? style.header : style.fill), rounding * scale);
        const float headerHeight = compact ? geometry.height : style.headerHeight;
        const float headerBottom = top.y + style.headerHeight * scale;
        if (!compact)
        {
            draw->AddRectFilled(top, {bottom.x, headerBottom}, Color(style.header), style.rounding * scale,
                                ImDrawFlags_RoundCornersTop);
            draw->AddRectFilledMultiColor({top.x, top.y + style.rounding * scale}, {bottom.x, headerBottom},
                                          Color(style.header), Color(style.header), Color(style.headerBottom),
                                          Color(style.headerBottom));
            draw->AddLine({top.x, headerBottom}, {bottom.x, headerBottom}, Color(style.separator), scale);
        }
        draw->AddRect(top, bottom, Color(nodeSelected ? style.selectedBorder : style.border), rounding * scale, 0,
                      (nodeSelected ? style.selectedBorderThickness : style.borderThickness) * scale);
        const float textSize = style.fontSize * scale;
        const float chevronX = top.x + 11.0f * scale;
        const float chevronY = top.y + headerHeight * 0.5f * scale;
        if (!style.headerOnly && layout.collapsed)
        {
            draw->AddLine({chevronX - 2.0f * scale, chevronY - 4.0f * scale}, {chevronX + 2.0f * scale, chevronY},
                          Color(style.text), 1.5f * scale);
            draw->AddLine({chevronX + 2.0f * scale, chevronY}, {chevronX - 2.0f * scale, chevronY + 4.0f * scale},
                          Color(style.text), 1.5f * scale);
        }
        else if (!style.headerOnly)
        {
            draw->AddLine({chevronX - 4.0f * scale, chevronY - 2.0f * scale}, {chevronX, chevronY + 2.0f * scale},
                          Color(style.text), 1.5f * scale);
            draw->AddLine({chevronX, chevronY + 2.0f * scale}, {chevronX + 4.0f * scale, chevronY - 2.0f * scale},
                          Color(style.text), 1.5f * scale);
        }
        const float textY =
            (headerHeight - style.fontSize) * 0.5f + style.headerTextOffsetY - LXNodeStyle::DefaultHeaderTextOffsetY;
        float titleX = style.headerOnly ? style.textPaddingX : 22.0f;
        if (style.headerOnly)
        {
            const float titleWidth = ImGui::GetFont()->CalcTextSizeA(textSize, FLT_MAX, 0.0f, node.title.c_str()).x;
            titleX = std::max(style.textPaddingX, (geometry.width - titleWidth / scale) * 0.5f);
        }
        const ImVec4 headerClip{top.x + titleX * scale, top.y, bottom.x - style.textPaddingX * scale,
                                top.y + headerHeight * scale};
        draw->AddText(ImGui::GetFont(), textSize, Add(top, Mul({titleX, textY}, scale)), Color(style.text),
                      node.title.c_str(), nullptr, 0.0f, &headerClip);

        if (style.showPropertyPreview && !compact)
        {
            for (const std::string& key : items.Keys(node))
            {
                const std::string value = ItemValue(node, key, items);
                const LXNodeItemRect rect = ItemRect(node, layout, key, style, items);
                const ImVec2 itemTop = ToScreen(origin, state, rect.minimum, scale);
                const ImVec2 itemBottom = ToScreen(origin, state, rect.maximum, scale);
                const LXNodeItemSpec* spec = items.Find(node, key);
                const LXNodeItemKind kind = spec ? spec->kind : LXNodeItemKind::Text;

                if (kind == LXNodeItemKind::Card)
                {
                    const LXItemStyle& card = styles.ForItem(spec->styleRole);
                    draw->AddRectFilled(itemTop, itemBottom, Color(card.fill), card.rounding * scale);
                    draw->AddRect(itemTop, itemBottom, Color(card.border), card.rounding * scale);
                    const ImVec4 titleClip{itemTop.x + 6.0f * scale, itemTop.y, itemBottom.x - 6.0f * scale,
                                           itemTop.y + 20.0f * scale};
                    const ImVec4 detailClip{itemTop.x + 6.0f * scale, itemTop.y + 17.0f * scale,
                                            itemBottom.x - 6.0f * scale, itemBottom.y};
                    const char* title = spec->label == "$node" ? node.title.c_str()
                                        : spec->label.empty()  ? key.c_str()
                                                               : spec->label.c_str();
                    draw->AddText(ImGui::GetFont(), textSize, {itemTop.x + 6.0f * scale, itemTop.y + 3.0f * scale},
                                  Color(card.title), title, nullptr, 0.0f, &titleClip);
                    draw->AddText(ImGui::GetFont(), textSize * 0.75f,
                                  {itemTop.x + 6.0f * scale, itemTop.y + 19.0f * scale}, Color(card.detail),
                                  value.c_str(), nullptr, 0.0f, &detailClip);
                    continue;
                }
                draw->AddRectFilled(itemTop, itemBottom, Color(style.propertyFill), style.rounding * scale);
                draw->AddRect(itemTop, itemBottom, Color(style.border), style.rounding * scale);
                if (kind == LXNodeItemKind::TexturePreview)
                {
                    const float inset = 3.0f * scale;
                    DrawTexturePreview(draw, Add(itemTop, {inset, inset}), Sub(itemBottom, {inset, inset}), value,
                                       items.ResolveTexture(value), textSize * 0.8f);
                    continue;
                }

                const float padding = style.propertyMargin * scale;
                const float labelY = itemTop.y + (itemBottom.y - itemTop.y - textSize * 0.85f) * 0.5f;
                if (kind == LXNodeItemKind::Text)
                {
                    const std::string caption = key + "  " + value;
                    const ImVec4 clip{itemTop.x + padding, itemTop.y, itemBottom.x - padding, itemBottom.y};
                    draw->AddText(ImGui::GetFont(), textSize * 0.85f, {itemTop.x + padding, labelY}, Color(style.text),
                                  caption.c_str(), nullptr, 0.0f, &clip);
                    continue;
                }

                draw->AddText(ImGui::GetFont(), textSize * 0.85f, {itemTop.x + padding, labelY}, Color(style.text),
                              key.c_str());
                const std::string nodeKey = std::to_string(node.id);
                ImGui::PushID(nodeKey.c_str());
                ImGui::PushID(key.c_str());
                ImGui::BeginDisabled(!graph.IsNodeEditable(node.id));
                if (kind == LXNodeItemKind::FloatSlider)
                {
                    const float controlX = itemTop.x + (itemBottom.x - itemTop.x) * 0.40f;
                    const float controlWidth = itemBottom.x - controlX - padding;
                    const Pin* input = ItemInputPin(node, key, items);
                    const bool connected =
                        input && std::any_of(graph.Links().begin(), graph.Links().end(),
                                             [&](const Link& link) { return link.input == input->id; });
                    if (connected)
                    {
                        if (state.sliderNode == node.id && state.sliderKey == key)
                        {
                            state.sliderNode = 0;
                            state.sliderKey.clear();
                        }
                        draw->AddText(ImGui::GetFont(), textSize * 0.8f, {controlX, labelY}, Color(style.text),
                                      "Linked");
                    }
                    else
                    {
                        ImGui::SetCursorScreenPos({controlX, itemTop.y + 3.0f * scale});
                        ImGui::SetNextItemWidth(controlWidth);
                        const bool editing = state.sliderNode == node.id && state.sliderKey == key;
                        const Pin* valuePin = items.ValuePin(node, key);
                        float slider = editing ? state.sliderValue : ReadFloat(value);
                        const float minimum = spec ? spec->minimum : 0.0f;
                        const float maximum = spec ? spec->maximum : 1.0f;
                        if (ImGui::SliderFloat("##slider", &slider, minimum, maximum, "%.2f"))
                        {
                            state.sliderNode = node.id;
                            state.sliderKey = key;
                            state.sliderValue = slider;
                        }
                        if (ImGui::IsItemDeactivatedAfterEdit())
                        {
                            const bool changed = valuePin && valuePin->type == PinType::Float
                                                     ? setSocketValue(valuePin->id, static_cast<double>(slider))
                                                     : setProperty(node.id, key, WriteFloat(slider));
                            if (changed)
                            {
                                state.dirty = true;
                            }
                            state.sliderNode = 0;
                            state.sliderKey.clear();
                        }
                    }
                }
                else if (kind == LXNodeItemKind::Color)
                {
                    const float buttonSize = itemBottom.y - itemTop.y - 6.0f * scale;
                    const float buttonX = itemBottom.x - padding - buttonSize;
                    ImGui::SetCursorScreenPos({buttonX, itemTop.y + 3.0f * scale});
                    const ImVec4 swatch = ItemColor(node, key, items);
                    if (ImGui::ColorButton("##swatch", swatch, ImGuiColorEditFlags_NoTooltip, {buttonSize, buttonSize}))
                    {
                        state.colorNode = node.id;
                        state.colorKey = key;
                        state.colorValue = swatch;
                        ImGui::OpenPopup("##color_picker");
                    }
                    if (ImGui::BeginPopup("##color_picker"))
                    {
                        float channels[4] = {state.colorValue.x, state.colorValue.y, state.colorValue.z,
                                             state.colorValue.w};
                        if (ImGui::ColorPicker4("##picker", channels,
                                                ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_PickerHueBar))
                        {
                            state.colorValue = {channels[0], channels[1], channels[2], channels[3]};
                        }
                        if (ImGui::Button("Apply"))
                        {
                            const Pin* valuePin = items.ValuePin(node, key);
                            const bool changed =
                                valuePin && valuePin->type == PinType::Color
                                    ? setSocketValue(valuePin->id,
                                                     std::array<double, 4>{state.colorValue.x, state.colorValue.y,
                                                                           state.colorValue.z, state.colorValue.w})
                                    : setProperty(node.id, key, WriteColor(state.colorValue));
                            if (changed)
                            {
                                state.dirty = true;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cancel"))
                        {
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
                ImGui::EndDisabled();
                ImGui::PopID();
                ImGui::PopID();
            }
        }

        for (const Pin& pin : node.pins)
        {
            if (style.headerOnly && hasHeaderConnectionOnDefaultSide(pin))
            {
                continue;
            }
            const ImVec2 point = ToScreen(origin, state, PinPosition(node, layout, pin, style, &items), scale);
            if (compact || style.pinLayout == LXPinLayout::TopBottom)
            {
                DrawPin(draw, point, pin.direction, styles.ForPin(pin), scale, state.pendingPin == pin.id,
                        style.pinLayout == LXPinLayout::TopBottom);
                continue;
            }
            const ImVec2 labelSize =
                ImGui::GetFont()->CalcTextSizeA(textSize, FLT_MAX, 0.0f, pin.name.c_str(), nullptr);
            const float labelX = pin.direction == Direction::Input ? point.x + style.textPaddingX * scale
                                                                   : point.x - style.textPaddingX * scale - labelSize.x;
            const ImVec4 labelClip = pin.direction == Direction::Input
                                         ? ImVec4{top.x + style.textPaddingX * scale, point.y - textSize,
                                                  top.x + geometry.width * scale * 0.5f, point.y + textSize}
                                         : ImVec4{top.x + geometry.width * scale * 0.5f, point.y - textSize,
                                                  bottom.x - style.textPaddingX * scale, point.y + textSize};
            bool itemLabel = false;
            for (const std::string& key : items.Keys(node))
            {
                if (ItemInputPin(node, key, items) == &pin)
                {
                    itemLabel = true;
                    break;
                }
            }
            if (!itemLabel)
            {
                draw->AddText(ImGui::GetFont(), textSize, {labelX, point.y - textSize * 0.5f}, Color(style.text),
                              pin.name.c_str(), nullptr, 0.0f, &labelClip);
            }
            const bool active = pin.id == hoveredPin || pin.id == state.pendingPin;
            DrawPin(draw, point, pin.direction, styles.ForPin(pin), scale, active, false);
        }
    }
    forEachHeaderPort([&](const Pin& pin, ImVec2 position) {
        DrawPin(draw, ToScreen(origin, state, position, scale), pin.direction, styles.ForPin(pin), scale,
                pin.id == hoveredPin || pin.id == state.pendingPin, false);
    });
    if (state.pendingPin)
    {
        const Pin* pin = graph.FindPin(state.pendingPin);
        const Node* owner = Owner(graph, state.pendingPin);
        const NodeLayout* layout = owner ? graph.FindLayout(owner->id) : nullptr;
        if (pin && owner && layout)
        {
            const LXNodeStyle& style = styles.ForNode(*owner);
            Wire(draw, ToScreen(origin, state, state.pendingAnchor, scale), mouse, styles.ForWire({}, pin->type), true,
                 scale, style.pinLayout == LXPinLayout::TopBottom);
        }
    }
    draw->PopClipRect();
    ImGui::SetCursorScreenPos(cursorAfterCanvas);
    ImGui::Dummy({0.0f, 0.0f});

    if (hoveredPin)
    {
        const Pin* pin = graph.FindPin(hoveredPin);
        if (pin)
        {
            ImGui::BeginTooltip();
            ImGui::Text("%s: %s", pin->name.c_str(), PinTypeName(pin->type));
            ImGui::EndTooltip();
        }
    }
}

} // namespace LX
