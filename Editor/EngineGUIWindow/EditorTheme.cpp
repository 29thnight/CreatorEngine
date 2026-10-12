#include "EditorTheme.h"

#include "ImGui.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace editor
{
    namespace
    {
        struct ColorToken { const char* name; std::uint32_t rgb; };
        constexpr std::array<ColorToken, static_cast<std::size_t>(ThemeColor::Count)> kColors{{
            { "Canvas", 0x181818 }, { "Chrome", 0x262626 },
            { "Panel", 0x2E2E2E }, { "PanelRaised", 0x383838 },
            { "Selection", 0x454545 }, { "Border", 0x3D3D3D },
            { "BorderStrong", 0x505050 }, { "Primary", 0x4B91E2 },
            { "Text", 0xE6E6E6 }, { "TextMuted", 0xA9A9A9 },
            { "TextDisabled", 0x777777 }, { "Positive", 0x7ABF83 },
            { "Warning", 0xD6AD5C }, { "Error", 0xE47777 },
        }};

        float valid_scale(float scale) noexcept
        {
            return std::isfinite(scale) && scale > 0.f ? scale : 1.f;
        }
    }

    std::uint32_t ThemeColorHex(ThemeColor color) noexcept
    {
        const auto index = static_cast<std::size_t>(color);
        return index < kColors.size() ? kColors[index].rgb : 0;
    }

    const char* ThemeColorName(ThemeColor color) noexcept
    {
        const auto index = static_cast<std::size_t>(color);
        return index < kColors.size() ? kColors[index].name : "Unknown";
    }

    ImVec4 ThemeColorValue(ThemeColor color, float alpha) noexcept
    {
        const std::uint32_t rgb = ThemeColorHex(color);
        return ImVec4(((rgb >> 16) & 0xff) / 255.f,
                      ((rgb >> 8) & 0xff) / 255.f,
                      (rgb & 0xff) / 255.f, alpha);
    }

    float ThemePixels(float logicalPixels) noexcept
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        return logicalPixels * style.FontScaleMain * style.FontScaleDpi;
    }

    float TitleBarFramePaddingY(float fontHeight, float safePaddingY, float framebufferScaleY) noexcept
    {
        // Win32 client coordinates already are physical pixels in our per-monitor
        // aware hosts (framebuffer scale 1). Do not multiply this floor by DPI or
        // user scale again. Larger scaled fonts/safe areas may still grow the row.
        // ImGui truncates window sizes, so round the reserved coordinate upward.
        const float paddedFontHeight = fontHeight + 2.f * std::max(0.f, safePaddingY);
        const float physicalMinimum = EditorThemeTokens::TitleBarHeight / valid_scale(framebufferScaleY);
        const float minimumHeight = std::ceil(std::max(physicalMinimum, paddedFontHeight));
        return (minimumHeight - fontHeight) * .5f;
    }

    TabStyleScope::TabStyleScope()
        : m_paddingY(ImGui::GetStyle().FramePadding.y)
    {
        // Do not carry an ImGui style-stack entry across NewFrame.
        ImGui::GetStyle().FramePadding.y = std::trunc(ThemePixels(
            (EditorThemeTokens::TabHeight - EditorThemeTokens::BodyFontSize) * 0.5f));
    }

    TabStyleScope::~TabStyleScope()
    {
        ImGui::GetStyle().FramePadding.y = m_paddingY;
    }

    namespace
    {
        int inspectorStyleDepth = 0;
        ImVec4 theme_rgb(std::uint32_t rgb)
        {
            return ImVec4(((rgb >> 16) & 255) / 255.f,
                ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1.f);
        }
    }

    bool InspectorStyleActive() noexcept { return inspectorStyleDepth > 0; }

    InspectorStyleScope::InspectorStyleScope()
    {
        ++inspectorStyleDepth;
        using T = InspectorThemeTokens;
        ImGui::PushStyleColor(ImGuiCol_Text, ThemeColorValue(ThemeColor::Text));
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, theme_rgb(T::Label));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme_rgb(T::Field));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ThemeColorValue(ThemeColor::Panel));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ThemeColorValue(ThemeColor::PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_CheckboxSelectedBg, ThemeColorValue(ThemeColor::Primary, 0.18f));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeColorValue(ThemeColor::Border));
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ThemeColorValue(ThemeColor::PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ThemeColorValue(ThemeColor::Selection));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ThemePixels(6.f), ThemePixels(T::RowGap)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
            ImVec2(ThemePixels(T::AxisGap), ThemePixels(T::RowGap)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ThemePixels(3.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    }

    InspectorStyleScope::~InspectorStyleScope()
    {
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(10);
        --inspectorStyleDepth;
    }

    HierarchyStyleScope::HierarchyStyleScope()
    {
        using T = HierarchyThemeTokens;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme_rgb(T::Row));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme_rgb(T::Row));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ThemeColorValue(ThemeColor::Panel));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ThemeColorValue(ThemeColor::PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_Header, theme_rgb(T::SelectedRow));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ThemeColorValue(ThemeColor::PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme_rgb(T::SelectedRow));
        ImGui::PushStyleColor(ImGuiCol_Button, theme_rgb(T::Row));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ThemeColorValue(ThemeColor::PanelRaised));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ThemeColorValue(ThemeColor::Selection));
        ImGui::PushStyleColor(ImGuiCol_Border, ThemeColorValue(ThemeColor::Border));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
            ImVec2(ThemePixels(T::ToolbarGap), ThemePixels(T::ToolbarGap)));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ThemePixels(3.f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ThemePixels(3.f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    }

    HierarchyStyleScope::~HierarchyStyleScope()
    {
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(11);
    }

    void ApplyEditorTheme(ImGuiStyle& style, float userScale, float dpiScale) noexcept
    {
        const float fontSizeBase = style.FontSizeBase;
        style = ImGuiStyle{};
        style.FontSizeBase = fontSizeBase;
        style.FontScaleMain = valid_scale(userScale);
        style.FontScaleDpi = valid_scale(dpiScale);

        using T = EditorThemeTokens;
        // Body controls use compact padding; TabStyleScope restores tab padding
        // only while ImGui lays out window titles and tab handles.
        style.WindowPadding = ImVec2(T::PanelPaddingX, T::PanelPaddingY);
        style.WindowRounding = 0.f;
        style.WindowBorderSize = T::PanelGap;
        style.WindowTitleAlign = ImVec2(0.f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ChildRounding = T::ControlRadius;
        style.ChildBorderSize = T::PanelGap;
        style.PopupRounding = T::ControlRadius;
        style.PopupBorderSize = T::PanelGap;
        style.FramePadding = ImVec2(T::ControlPaddingX,
            (T::ControlHeight - T::BodyFontSize) * 0.5f);
        style.FrameRounding = T::ControlRadius;
        style.FrameBorderSize = 1.f;
        style.ItemSpacing = ImVec2(T::ItemGapX, T::RowHeight - T::BodyFontSize);
        style.ItemInnerSpacing = ImVec2(T::ItemInnerGapX, T::ItemGapY);
        style.CellPadding = ImVec2(T::CellPaddingX, T::CompactGap);
        style.IndentSpacing = T::TreeIndent;
        style.ScrollbarSize = T::ScrollbarWidth;
        style.ScrollbarRounding = T::ControlRadius;
        style.GrabMinSize = T::ScrollbarWidth;
        style.GrabRounding = T::ControlRadius;
        style.TabRounding = T::ControlRadius;
        style.TabBarBorderSize = T::PanelGap;
        style.TabBarOverlineSize = T::TabActiveMarker;
        style.SeparatorTextBorderSize = T::PanelGap;
        style.DockingSeparatorSize = T::PanelGap;

        using C = ThemeColor;
        const auto set = [&style](ImGuiCol slot, C color, float alpha = 1.f)
        {
            style.Colors[slot] = ThemeColorValue(color, alpha);
        };
        set(ImGuiCol_Text, C::Text);
        set(ImGuiCol_TextDisabled, C::TextDisabled);
        set(ImGuiCol_WindowBg, C::Canvas);
        set(ImGuiCol_ChildBg, C::Panel);
        set(ImGuiCol_PopupBg, C::Panel);
        set(ImGuiCol_Border, C::Border);
        set(ImGuiCol_BorderShadow, C::Canvas, 0.f);
        // Flat neutral surfaces retain a visible step between fields and panels.
        // Blue is reserved for focus, selection markers and primary actions.
        set(ImGuiCol_FrameBg, C::Chrome);
        set(ImGuiCol_FrameBgHovered, C::PanelRaised);
        set(ImGuiCol_FrameBgActive, C::Selection);
        set(ImGuiCol_TitleBg, C::Chrome);
        set(ImGuiCol_TitleBgActive, C::Chrome);
        set(ImGuiCol_TitleBgCollapsed, C::Chrome);
        set(ImGuiCol_MenuBarBg, C::Chrome);
        set(ImGuiCol_ScrollbarBg, C::Canvas);
        set(ImGuiCol_ScrollbarGrab, C::BorderStrong);
        set(ImGuiCol_ScrollbarGrabHovered, C::Selection);
        set(ImGuiCol_ScrollbarGrabActive, C::Primary);
        set(ImGuiCol_CheckMark, C::Primary);
        set(ImGuiCol_CheckboxSelectedBg, C::Primary, 0.18f);
        set(ImGuiCol_SliderGrab, C::Primary);
        set(ImGuiCol_SliderGrabActive, C::Primary);
        set(ImGuiCol_Button, C::Panel);
        set(ImGuiCol_ButtonHovered, C::PanelRaised);
        set(ImGuiCol_ButtonActive, C::Selection);
        set(ImGuiCol_Header, C::Panel);
        set(ImGuiCol_HeaderHovered, C::Selection);
        set(ImGuiCol_HeaderActive, C::Selection);
        set(ImGuiCol_Separator, C::Border);
        set(ImGuiCol_SeparatorHovered, C::Primary);
        set(ImGuiCol_SeparatorActive, C::Primary);
        set(ImGuiCol_ResizeGrip, C::Canvas, 0.f);
        set(ImGuiCol_ResizeGripHovered, C::Primary, 0.55f);
        set(ImGuiCol_ResizeGripActive, C::Primary, 0.9f);
        set(ImGuiCol_InputTextCursor, C::Text);
        set(ImGuiCol_Tab, C::Chrome);
        set(ImGuiCol_TabHovered, C::PanelRaised);
        set(ImGuiCol_TabSelected, C::Panel);
        set(ImGuiCol_TabSelectedOverline, C::Primary);
        set(ImGuiCol_TabDimmed, C::Chrome);
        set(ImGuiCol_TabDimmedSelected, C::Panel);
        set(ImGuiCol_TabDimmedSelectedOverline, C::BorderStrong);
        set(ImGuiCol_DockingPreview, C::Primary, 0.45f);
        set(ImGuiCol_DockingEmptyBg, C::Canvas);
        set(ImGuiCol_PlotLines, C::TextMuted);
        set(ImGuiCol_PlotLinesHovered, C::Primary);
        set(ImGuiCol_PlotHistogram, C::Primary);
        set(ImGuiCol_PlotHistogramHovered, C::Positive);
        set(ImGuiCol_TableHeaderBg, C::Panel);
        set(ImGuiCol_TableBorderStrong, C::BorderStrong);
        set(ImGuiCol_TableBorderLight, C::Border);
        set(ImGuiCol_TableRowBg, C::Canvas, 0.f);
        set(ImGuiCol_TableRowBgAlt, C::Panel, 0.5f);
        set(ImGuiCol_TextLink, C::Primary);
        set(ImGuiCol_TextSelectedBg, C::Primary, 0.35f);
        set(ImGuiCol_TreeLines, C::Border);
        set(ImGuiCol_DragDropTarget, C::Primary);
        set(ImGuiCol_DragDropTargetBg, C::Primary, 0.15f);
        set(ImGuiCol_UnsavedMarker, C::Warning);
        set(ImGuiCol_NavCursor, C::Primary);
        set(ImGuiCol_NavWindowingHighlight, C::Primary, 0.7f);
        set(ImGuiCol_NavWindowingDimBg, C::Canvas, 0.7f);
        set(ImGuiCol_ModalWindowDimBg, C::Canvas, 0.7f);

        style.ScaleAllSizes(style.FontScaleMain * style.FontScaleDpi);
    }
}
