#pragma once

#include "../Core/LXGraph.h"

#include <imgui.h>

#include <map>
#include <optional>
#include <iosfwd>
#include <string>

namespace LX
{

enum class LXPinLayout
{
    Sides,
    TopBottom
};

// Editor presentation only. None of these values belongs to the executable graph.
struct LXNodeStyle
{
    static constexpr float DefaultHeaderTextOffsetY = 3.0f;

    ImVec4 fill{0.205f, 0.205f, 0.205f, 1.0f};
    ImVec4 header{0.310f, 0.310f, 0.310f, 1.0f};
    ImVec4 headerBottom{0.275f, 0.275f, 0.275f, 1.0f};
    ImVec4 border{0.115f, 0.115f, 0.115f, 1.0f};
    ImVec4 selectedBorder{0.965f, 0.584f, 0.235f, 1.0f};
    ImVec4 separator{0.145f, 0.145f, 0.145f, 1.0f};
    ImVec4 text{0.875f, 0.875f, 0.875f, 1.0f};
    ImVec4 propertyFill{0.275f, 0.275f, 0.275f, 1.0f};
    float width = 180.0f;
    float height = 0.0f;
    float headerHeight = 22.0f;
    float rowHeight = 24.0f;
    float bodyBottomPadding = 8.0f;
    float propertyPreviewHeight = 24.0f;
    float propertyMargin = 6.0f;
    float rounding = 4.0f;
    float borderThickness = 1.0f;
    float selectedBorderThickness = 2.0f;
    float fontSize = 12.0f;
    float textPaddingX = 10.0f;
    float headerTextOffsetY = DefaultHeaderTextOffsetY;
    bool showPropertyPreview = true;
    bool headerOnly = false;
    LXPinLayout pinLayout = LXPinLayout::Sides;
};

struct LXItemStyle
{
    ImVec4 fill{0.275f, 0.275f, 0.275f, 1.0f};
    ImVec4 border{0.115f, 0.115f, 0.115f, 1.0f};
    ImVec4 title{0.95f, 0.95f, 0.95f, 1.0f};
    ImVec4 detail{0.72f, 0.72f, 0.72f, 1.0f};
    float rounding = 3.0f;
};

enum class LXPinShape
{
    Circle,
    Triangle,
    Diamond
};

struct LXPinStyle
{
    LXPinShape shape = LXPinShape::Circle;
    ImVec4 fill{0.650f, 0.650f, 0.650f, 1.0f};
    ImVec4 border{0.140f, 0.140f, 0.140f, 1.0f};
    float radius = 4.5f;
    float hitPadding = 6.0f;
    float borderThickness = 1.0f;
    bool visible = true;
};

struct LXWireStyle
{
    enum class Route
    {
        Bezier,
        Straight
    };

    ImVec4 color{0.585f, 0.585f, 0.585f, 0.950f};
    ImVec4 selectedColor{0.925f, 0.925f, 0.925f, 1.0f};
    float thickness = 1.5f;
    float selectedThickness = 2.5f;
    float bend = 36.0f;
    Route route = Route::Bezier;
    bool arrowAtMidpoint = false;
};

struct LXCanvasStyle
{
    ImVec4 background{0.145f, 0.145f, 0.145f, 1.0f};
    ImVec4 grid{0.215f, 0.215f, 0.215f, 0.550f};
    ImVec4 shadow{0.0f, 0.0f, 0.0f, 0.350f};
    float gridSpacing = 24.0f;
    float shadowOffset = 2.0f;
    float minZoom = 0.45f;
    float maxZoom = 2.2f;
    float zoomStep = 1.12f;
    bool showGrid = true;
};

struct LXFrameStyle
{
    ImVec4 fill{0.205f, 0.230f, 0.255f, 0.370f};
    ImVec4 header{0.305f, 0.350f, 0.390f, 0.750f};
    ImVec4 border{0.475f, 0.520f, 0.570f, 0.700f};
    ImVec4 selectedBorder{0.790f, 0.845f, 0.890f, 1.0f};
    ImVec4 text{0.920f, 0.935f, 0.950f, 1.0f};
    float headerHeight = 24.0f;
    float rounding = 5.0f;
    float borderThickness = 1.0f;
    float selectedBorderThickness = 2.0f;
};

class LXStyleSheet
{
  public:
    LXCanvasStyle canvas;
    LXFrameStyle frame;
    LXNodeStyle defaultNode;
    LXPinStyle defaultPin;
    LXWireStyle defaultWire;

    const LXNodeStyle& ForNodeType(const std::string& type) const;
    const LXNodeStyle& ForNode(const Node& node) const;
    const LXPinStyle& ForPin(const Pin& pin) const;
    const LXWireStyle& ForWire(const Link& link, PinType type) const;
    const LXItemStyle& ForItem(const std::string& role) const;
    void SetTypeStyle(std::string type, const LXNodeStyle& style);
    void SetNodeStyle(Id node, const LXNodeStyle& style);
    void SetPinTypeStyle(PinType type, const LXPinStyle& style);
    void SetPinStyle(Id pin, const LXPinStyle& style);
    void SetWireTypeStyle(PinType type, const LXWireStyle& style);
    void SetWireStyle(Id link, const LXWireStyle& style);
    void SetItemStyle(std::string role, const LXItemStyle& style);
    bool ClearTypeStyle(const std::string& type);
    bool ClearNodeStyle(Id node);
    bool ClearPinStyle(Id pin);
    bool ClearWireTypeStyle(PinType type);
    bool ClearWireStyle(Id link);
    bool HasTypeStyle(const std::string& type) const;
    bool HasNodeStyle(Id node) const;
    LXStyleSheet WithoutIndividualOverrides() const;
    bool Equals(const LXStyleSheet& other) const;
    bool Save(const std::string& path, std::string* error = nullptr) const;
    static std::optional<LXStyleSheet> Load(const std::string& path, std::string* error = nullptr);

  private:
    std::map<std::string, LXNodeStyle> typeStyles_;
    std::map<Id, LXNodeStyle> nodeStyles_;
    std::map<PinType, LXPinStyle> pinTypeStyles_;
    std::map<Id, LXPinStyle> pinStyles_;
    std::map<PinType, LXWireStyle> wireTypeStyles_;
    std::map<Id, LXWireStyle> wireStyles_;
    LXItemStyle defaultItem_;
    std::map<std::string, LXItemStyle> itemStyles_;
    static std::optional<LXStyleSheet> LoadExact(const std::string& path, std::string* error);
    void Write(std::ostream& out) const;
};

} // namespace LX
