#include "MaterialGraphPresentation.h"
#include "../../Lattice/Core/LXNodeDefinition.h"
#include "../ImGuiHelper/EditorIcons.h"
#include <imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <map>
#include <set>

namespace editor::material_editing
{
namespace
{
std::string Section(const std::string& identifier)
{
    for (const auto* section :
         {"Diffuse", "Subsurface", "Specular", "Transmission", "Coat", "Sheen", "Emission", "Thin Film"})
    {
        if (identifier.starts_with(section))
        {
            return section;
        }
    }
    if (identifier == "Anisotropic" || identifier == "Anisotropic Rotation" || identifier == "Tangent")
    {
        return "Specular";
    }
    return {};
}
} // namespace

namespace
{
enum class HeaderIcon
{
    None,
    Editor,
    Material,
    Object,
    Mesh,
    Save,
    New,
    Close,
    Apply,
    FrameAll,
    Snap,
    Overlays
};

const char* HeaderGlyph(HeaderIcon icon)
{
    switch (icon)
    {
    case HeaderIcon::Editor:
        return EditorIcon::NodeEditor;
    case HeaderIcon::Material:
        return EditorIcon::Lit;
    case HeaderIcon::Object:
        return EditorIcon::GameObject;
    case HeaderIcon::Mesh:
        return EditorIcon::Model;
    case HeaderIcon::Save:
        return EditorIcon::Shield;
    case HeaderIcon::New:
        return EditorIcon::Duplicate;
    case HeaderIcon::Close:
        return EditorIcon::Close;
    case HeaderIcon::Apply:
        return EditorIcon::Pin;
    case HeaderIcon::FrameAll:
        return EditorIcon::FrameAll;
    case HeaderIcon::Snap:
        return EditorIcon::Snap;
    case HeaderIcon::Overlays:
        return EditorIcon::Overlays;
    default:
        return nullptr;
    }
}

void DrawSymbol(ImDrawList* draw, const char* symbol, ImVec2 center, float pixels)
{
    if (!symbol)
    {
        return;
    }
    // The generated subset uses only three-byte UTF-8 private-use codepoints.
    const auto* bytes = reinterpret_cast<const unsigned char*>(symbol);
    const ImWchar codepoint = ((bytes[0] & 0x0f) << 12) | ((bytes[1] & 0x3f) << 6) | (bytes[2] & 0x3f);
    auto* font = ImGui::GetFont();
    const auto* glyph = font->GetFontBaked(pixels)->FindGlyphNoFallback(codepoint);
    if (!glyph)
    {
        return;
    }
    // Align the visible glyph, rather than its advance width or line box.
    const ImVec2 position{center.x - (glyph->X0 + glyph->X1) * 0.5f, center.y - (glyph->Y0 + glyph->Y1) * 0.5f};
    draw->AddText(font, pixels, position, IM_COL32(205, 205, 205, 255), symbol);
}

void DrawHeaderIcon(ImDrawList* draw, HeaderIcon icon, ImVec2 center, float scale)
{
    DrawSymbol(draw, HeaderGlyph(icon), center, 18.0f * scale);
}

void DrawControlFrame(ImVec2 start, float width, bool selected = false)
{
    const float scale = LX::CanvasUiScale();
    const ImVec2 end{start.x + width, start.y + ImGui::GetFrameHeight()};
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(start, end, selected ? IM_COL32(72, 116, 180, 255) : IM_COL32(42, 42, 42, 255), 3.0f * scale);
    draw->AddRect(start, end, IM_COL32(66, 66, 66, 255), 3.0f * scale, 0, scale);
}

void DrawChevron(ImDrawList* draw, ImVec2 center)
{
    DrawSymbol(draw, EditorIcon::Expand, center, 10.0f * LX::CanvasUiScale());
}

bool IconButton(const char* id, HeaderIcon icon, const char* tooltip)
{
    const float scale = LX::CanvasUiScale();
    const auto start = ImGui::GetCursorScreenPos();
    const float size = ImGui::GetFrameHeight();
    const bool pressed = ImGui::Button(id, {size, size});
    DrawHeaderIcon(ImGui::GetWindowDrawList(), icon, {start.x + size * 0.5f, start.y + size * 0.5f}, scale);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

bool BeginHeaderCombo(const char* id, HeaderIcon icon, const char* label, float width, bool framed = true)
{
    auto* draw = ImGui::GetWindowDrawList();
    const auto start = ImGui::GetCursorScreenPos();
    const float scale = LX::CanvasUiScale();
    const float height = ImGui::GetFrameHeight();
    const float pixels = width * scale;
    if (framed)
    {
        DrawControlFrame(start, pixels);
    }
    ImGui::SetNextItemWidth(pixels);
    // Keep the standard combo interaction, but draw a small chevron instead of
    // the font-sized triangle and separate arrow tile from the global theme.
    const bool open = ImGui::BeginCombo(id, "", ImGuiComboFlags_NoArrowButton);
    if (icon != HeaderIcon::None)
    {
        DrawHeaderIcon(draw, icon, {start.x + 12.0f * scale, start.y + height * 0.5f}, scale);
    }
    const float textOffset = (icon == HeaderIcon::None ? 8.0f : 28.0f) * scale;
    draw->AddText({start.x + textOffset, start.y + (height - ImGui::GetFontSize()) * 0.5f},
                  IM_COL32(210, 210, 210, 255), label);
    DrawChevron(draw, {start.x + pixels - 9.0f * scale, start.y + height * 0.5f});
    return open;
}
} // namespace

std::vector<MaterialNodeMenuEntry> MaterialNodeMenuEntries(const LX::LXMaterialDefinitions& definitions,
                                                           std::string_view search)
{
    std::map<std::string, std::size_t> titleCounts;
    for (const auto& [type, schema] : definitions.schemas)
    {
        const auto* definition = definitions.nodes->Find(type);
        if (definition && definition->creatable)
        {
            ++titleCounts[definition->defaults.title];
        }
    }

    std::vector<MaterialNodeMenuEntry> entries;
    for (const auto& [type, schema] : definitions.schemas)
    {
        const auto* definition = definitions.nodes->Find(type);
        if (!definition || !definition->creatable)
        {
            continue;
        }
        const auto& spec = definition->defaults;
        if (!search.empty() && type.find(search) == std::string::npos && spec.title.find(search) == std::string::npos)
        {
            continue;
        }
        const std::string socketType = spec.pins.empty() ? type : LX::PinTypeName(spec.pins.front().type);
        if (type.starts_with("LXReroute"))
        {
            entries.push_back({type, socketType, "Reroute"});
        }
        else
        {
            const auto label = titleCounts.at(spec.title) > 1 ? spec.title + " (" + socketType + ")" : spec.title;
            entries.push_back({type, label, {}});
        }
    }
    return entries;
}

std::optional<std::string> DrawMaterialNodeAddMenu(const LX::LXMaterialDefinitions& definitions, std::string& search)
{
    ImGui::InputTextWithHint("##node_search", "Search nodes...", &search);
    const auto entries = MaterialNodeMenuEntries(definitions, search);
    std::optional<std::string> selected;
    const auto drawEntry = [&](const MaterialNodeMenuEntry& entry) {
        // Labels describe the node; the registered type owns the widget identity.
        ImGui::PushID(entry.type.c_str());
        if (ImGui::MenuItem(entry.label.c_str()))
        {
            selected = entry.type;
        }
        ImGui::PopID();
    };
    std::set<std::string> drawnGroups;
    for (const auto& entry : entries)
    {
        if (entry.group.empty())
        {
            drawEntry(entry);
        }
        else if (drawnGroups.insert(entry.group).second && ImGui::BeginMenu(entry.group.c_str()))
        {
            for (const auto& child : entries)
            {
                if (child.group == entry.group)
                {
                    drawEntry(child);
                }
            }
            ImGui::EndMenu();
        }
    }
    if (entries.empty())
    {
        ImGui::TextDisabled("No matching nodes");
    }
    return selected;
}

MaterialHeaderScope::MaterialHeaderScope(ImVec4 background)
{
    const float scale = LX::CanvasUiScale();
    // PushFont applies both user scale and monitor DPI itself.
    ImGui::PushFont(nullptr, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4.0f * scale, 3.0f * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{6.0f * scale, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.28f, 0.28f, 0.28f, 0.65f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{0.35f, 0.35f, 0.35f, 0.75f});
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, background);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{});
}

MaterialHeaderScope::~MaterialHeaderScope()
{
    ImGui::PopStyleColor(8);
    ImGui::PopStyleVar(5);
    ImGui::PopFont();
}

void DrawMaterialContextSelector()
{
    if (BeginHeaderCombo("##editor_type", HeaderIcon::Editor, "", 34.0f))
    {
        ImGui::MenuItem("Material Node Editor", nullptr, true);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (BeginHeaderCombo("##material_context", HeaderIcon::Object, "Object", 104.0f))
    {
        ImGui::MenuItem("Object", nullptr, true);
        ImGui::EndCombo();
    }
}

MaterialBarAction DrawMaterialDataBar(std::string& name, bool dirty, bool editable, bool showGrid, bool snapToGrid,
                                      const std::function<void()>& materialMenu,
                                      const std::function<void()>& overlayMenu)
{
    const float scale = LX::CanvasUiScale();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{});
    const float tile = ImGui::GetFrameHeight();
    const float right = ImGui::GetWindowWidth() - 2.0f * tile - 23.0f * scale;
    const float left = ImGui::GetCursorPosX() + 12.0f * scale;
    const float fixed = 114.0f * scale + 4.0f * tile + 10.0f * scale;
    const float nameWidth = std::clamp(right - left - fixed - 8.0f * scale, 44.0f * scale, 128.0f * scale);
    const float group = fixed + nameWidth;
    ImGui::SetCursorPosX(std::max(left, std::min((ImGui::GetWindowWidth() - group) * 0.5f, right - group)));
    if (BeginHeaderCombo("##material_slot", HeaderIcon::None, "Slot 1", 76.0f))
    {
        ImGui::Selectable("Slot 1", true);
        ImGui::EndCombo();
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
    const auto dataStart = ImGui::GetCursorScreenPos();
    const float dataWidth = 32.0f * scale + nameWidth + 3.0f * tile;
    DrawControlFrame(dataStart, dataWidth);
    if (BeginHeaderCombo("##material_data", HeaderIcon::Material, "", 32.0f, false))
    {
        if (materialMenu)
        {
            materialMenu();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!editable);
    ImGui::SetNextItemWidth(nameWidth);
    ImGui::InputText("##material_name", &name);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s%s", name.c_str(), dirty ? " (unsaved graph)" : "");
    }
    MaterialBarAction action = MaterialBarAction::None;
    ImGui::SameLine();
    if (IconButton("##save_graph", HeaderIcon::Save, dirty ? "Save Graph (unsaved edits)" : "Save Graph"))
    {
        action = MaterialBarAction::Save;
    }
    ImGui::SameLine();
    if (IconButton("##new_graph", HeaderIcon::New, "New Material Graph"))
    {
        action = MaterialBarAction::NewGraph;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (IconButton("##close_material", HeaderIcon::Close, "Close Material Editor (keep document)"))
    {
        action = MaterialBarAction::ClosePanel;
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
    ImGui::BeginDisabled(!editable);
    if (IconButton("##apply_graph", HeaderIcon::Apply, "Apply to MeshRenderer"))
    {
        action = MaterialBarAction::Apply;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + 8.0f * scale, right));
    DrawControlFrame(ImGui::GetCursorScreenPos(), tile, snapToGrid);
    if (IconButton("##snap_to_grid", HeaderIcon::Snap, "Snap Nodes to Grid"))
    {
        action = MaterialBarAction::ToggleSnap;
    }
    ImGui::SameLine(0.0f, 6.0f * scale);
    DrawControlFrame(ImGui::GetCursorScreenPos(), tile + 16.0f * scale, showGrid);
    if (IconButton("##show_overlays", HeaderIcon::Overlays, "Show Grid"))
    {
        action = MaterialBarAction::ToggleGrid;
    }
    ImGui::SameLine();
    if (BeginHeaderCombo("##material_overlays", HeaderIcon::None, "", 16.0f, false))
    {
        if (ImGui::MenuItem("Grid", nullptr, showGrid))
        {
            action = MaterialBarAction::ToggleGrid;
        }
        if (overlayMenu)
        {
            overlayMenu();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    return action;
}

void DrawMaterialBreadcrumb(ImVec2 origin, ImVec2 size, const std::string& object, const std::string& mesh,
                            const std::string& material, const std::string& status)
{
    auto* draw = ImGui::GetWindowDrawList();
    const float scale = LX::CanvasUiScale();
    const float fontSize = ImGui::GetFontSize() * 0.86f;
    const float y = origin.y + 18.0f * scale;
    float x = origin.x + 12.0f * scale;
    draw->PushClipRect(origin, {origin.x + size.x, origin.y + size.y}, true);
    const auto entry = [&](HeaderIcon icon, const std::string& label, bool arrow) {
        if (arrow)
        {
            DrawSymbol(draw, EditorIcon::Collapse, {x + 2.0f * scale, y}, 14.0f * scale);
            x += 17.0f * scale;
        }
        DrawHeaderIcon(draw, icon, {x + 6.0f * scale, y}, scale);
        x += 22.0f * scale;
        draw->AddText(ImGui::GetFont(), fontSize, {x, y - fontSize * 0.5f}, IM_COL32(200, 200, 200, 255),
                      label.c_str());
        x += ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str()).x + 20.0f * scale;
    };
    entry(HeaderIcon::Object, object, false);
    entry(HeaderIcon::Mesh, mesh, true);
    entry(HeaderIcon::Material, material, true);
    if (!status.empty())
    {
        const auto newline = status.find('\n');
        const auto end = status.data() + (newline == std::string::npos ? status.size() : newline);
        draw->AddText(ImGui::GetFont(), fontSize, {origin.x + 12.0f * scale, origin.y + size.y - 22.0f * scale},
                      IM_COL32(190, 190, 190, 255), status.data(), end);
        if (ImGui::IsMouseHoveringRect({origin.x, origin.y + size.y - 24.0f * scale},
                                       {origin.x + size.x, origin.y + size.y}))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(500.0f * scale);
            ImGui::TextUnformatted(status.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    draw->PopClipRect();
}

LX::LXStyleSheet BlenderStyles(const LX::LXMaterialDefinitions& definitions)
{
    LX::LXStyleSheet styles;
    styles.canvas.background = {0.11f, 0.11f, 0.11f, 1.0f};
    styles.canvas.grid = {0.32f, 0.32f, 0.32f, 0.38f};
    styles.canvas.gridSpacing = 30.0f;
    styles.canvas.gridPattern = LX::LXGridPattern::Dots;
    styles.canvas.gridDotRadius = 0.65f;
    styles.defaultNode.width = 285.0f;
    styles.defaultNode.fontSize = 15.0f;
    styles.defaultNode.headerHeight = 25.0f;
    styles.defaultNode.rowHeight = 27.0f;
    styles.defaultNode.propertyPreviewHeight = 25.0f;
    styles.defaultNode.rounding = 6.0f;
    styles.defaultNode.bodyBottomPadding = 5.0f;
    styles.defaultNode.propertyMargin = 9.0f;
    styles.defaultNode.fill = {0.19f, 0.19f, 0.19f, 1.0f};
    styles.defaultNode.propertyFill = {0.29f, 0.29f, 0.29f, 1.0f};
    styles.defaultNode.selectedBorder = {1.0f, 0.43f, 0.06f, 1.0f};
    for (const auto& [type, schema] : definitions.schemas)
    {
        auto style = styles.defaultNode;
        if (type.find("Bsdf") != std::string::npos || type.find("Volume") != std::string::npos)
        {
            style.header = {0.17f, 0.38f, 0.17f, 1.0f};
        }
        else if (type == "ShaderNodeOutputMaterial")
        {
            style.width = 210.0f;
            style.header = {0.26f, 0.12f, 0.14f, 1.0f};
        }
        else if (type.find("Tex") != std::string::npos)
        {
            style.header = {0.39f, 0.21f, 0.13f, 1.0f};
        }
        else if (type == "ShaderNodeRGB")
        {
            style.header = {0.40f, 0.33f, 0.13f, 1.0f};
        }
        if (type == "ShaderNodeBsdfPrincipled")
        {
            style.width = 330.0f;
        }
        style.headerBottom = style.header;
        styles.SetTypeStyle(type, style);
    }
    for (const auto type : {LX::PinType::Closure, LX::PinType::Color, LX::PinType::Float, LX::PinType::Vector})
    {
        LX::LXPinStyle pin;
        pin.radius = 4.5f;
        pin.fill = type == LX::PinType::Closure  ? ImVec4{0.38f, 0.76f, 0.39f, 1.0f}
                   : type == LX::PinType::Color  ? ImVec4{0.78f, 0.78f, 0.16f, 1.0f}
                   : type == LX::PinType::Vector ? ImVec4{0.39f, 0.36f, 0.76f, 1.0f}
                                                 : ImVec4{0.65f, 0.65f, 0.65f, 1.0f};
        styles.SetPinTypeStyle(type, pin);
        LX::LXWireStyle wire;
        wire.color = pin.fill;
        wire.thickness = 2.0f;
        styles.SetWireTypeStyle(type, wire);
    }
    return styles;
}

LX::LXNodeItemRegistry MaterialItems(const LX::LXMaterialDefinitions& definitions)
{
    using namespace LX;
    LXNodeItemRegistry items;
    for (const auto& [type, schema] : definitions.schemas)
    {
        const auto* definition = definitions.nodes->Find(type);
        if (!definition)
        {
            continue;
        }
        const auto& spec = definition->defaults;
        std::vector<LXNodeRow> rows;
        for (const auto& [key, value] : spec.properties)
        {
            // Blender's renderer selector is reference metadata. CreatorEngine
            // supports only ALL and chooses its rendering route automatically.
            if (type == "ShaderNodeOutputMaterial" && key == "target")
            {
                continue;
            }
            if (type.starts_with("LXParameter") && key == "valueSource")
                continue;
            LXNodeItemSpec item;
            item.key = key;
            item.kind = LXNodeItemKind::String;
            const auto choices = schema.enumProperties.find(key);
            if (choices != schema.enumProperties.end())
            {
                item.kind = LXNodeItemKind::Choice;
                item.choices.assign(choices->second.begin(), choices->second.end());
            }
            items.Register(type, item);
            rows.push_back({key});
            if (key == "image")
            {
                item.key = "Preview";
                item.kind = LXNodeItemKind::TexturePreview;
                item.propertyKey = key;
                item.height = 100.0f;
                items.Register(type, item);
                rows.push_back({item.key});
            }
        }
        for (const auto& pin : spec.pins)
        {
            if (pin.direction == Direction::Output)
            {
                rows.push_back({{}, pin.Identifier(), Direction::Output});
                if (spec.pins.size() == 1 &&
                    (pin.type == PinType::Color || pin.type == PinType::Float || pin.type == PinType::Texture))
                {
                    LXNodeItemSpec item;
                    item.key = "Value";
                    item.valuePin = pin.Identifier();
                    item.kind = pin.type == PinType::Color     ? LXNodeItemKind::Color
                                : pin.type == PinType::Texture ? LXNodeItemKind::String
                                                               : LXNodeItemKind::FloatSlider;
                    items.Register(type, item);
                    rows.push_back({item.key});
                    if (pin.type == PinType::Texture)
                    {
                        item.key = "Preview";
                        item.kind = LXNodeItemKind::TexturePreview;
                        item.height = 100.0f;
                        items.Register(type, item);
                        rows.push_back({item.key});
                    }
                }
            }
        }
        std::string previousSection;
        for (std::size_t index = 0; index < spec.pins.size(); ++index)
        {
            const auto& pin = spec.pins[index];
            if (pin.direction != Direction::Input || (index < schema.sockets.size() && !schema.sockets[index].enabled))
            {
                continue;
            }
            const std::string section = type == "ShaderNodeBsdfPrincipled" ? Section(pin.Identifier()) : "";
            if (!section.empty() && section != previousSection)
            {
                LXNodeItemSpec heading;
                heading.key = section;
                heading.kind = LXNodeItemKind::Section;
                items.Register(type, heading);
                rows.push_back({section});
            }
            previousSection = section;
            LXNodeItemSpec item;
            item.key = pin.Identifier();
            item.inputPin = item.valuePin = pin.Identifier();
            item.kind = pin.type == PinType::Float    ? LXNodeItemKind::FloatSlider
                        : pin.type == PinType::Color  ? LXNodeItemKind::Color
                        : pin.type == PinType::Vector ? LXNodeItemKind::Vector
                        : pin.type == PinType::Bool   ? LXNodeItemKind::Boolean
                        : pin.type == PinType::Int    ? LXNodeItemKind::Integer
                                                      : LXNodeItemKind::Text;
            if (pin.Identifier().find("IOR") != std::string::npos)
            {
                item.minimum = 1.0f;
                item.maximum = 4.0f;
            }
            else if (pin.Identifier().find("Strength") != std::string::npos ||
                     pin.Identifier().find("Scale") != std::string::npos)
            {
                item.maximum = 10.0f;
            }
            else if (pin.Identifier().find("Thickness") != std::string::npos)
            {
                item.maximum = 1000.0f;
            }
            const bool hideValue = index < schema.sockets.size() && schema.sockets[index].hideValue;
            const bool control = !hideValue && item.kind != LXNodeItemKind::Text;
            if (control)
            {
                items.Register(type, item);
            }
            rows.push_back({control ? item.key : std::string{}, pin.Identifier(), Direction::Input, section});
        }
        items.RegisterRows(type, std::move(rows));
    }
    return items;
}
} // namespace editor::material_editing
