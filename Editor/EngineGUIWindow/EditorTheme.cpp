#include "EditorTheme.h"

#include "ImGui.h"

#include <array>
#include <cmath>

namespace editor
{
    namespace
    {
        struct ColorToken { const char* name; std::uint32_t rgb; };
        constexpr std::array<ColorToken, static_cast<std::size_t>(ThemeColor::Count)> kColors{{
            { "Canvas", 0x181818 }, { "Chrome", 0x2A2A2A },
            { "Panel", 0x343434 }, { "PanelRaised", 0x484848 },
            { "Selection", 0x525252 }, { "Border", 0x3E3E3E },
            { "BorderStrong", 0x484848 }, { "Primary", 0x2E70EA },
            { "Text", 0xFFFFFF }, { "TextMuted", 0x9E9E9E },
            { "TextDisabled", 0x999999 }, { "Positive", 0x5AEB5C },
            { "Warning", 0xE6DB74 }, { "Error", 0xFB5A5A },
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

    void ApplyEditorTheme(ImGuiStyle& style, float userScale, float dpiScale) noexcept
    {
        const float fontSizeBase = style.FontSizeBase;
        style = ImGuiStyle{};
        style.FontSizeBase = fontSizeBase;
        style.FontScaleMain = valid_scale(userScale);
        style.FontScaleDpi = valid_scale(dpiScale);

        using T = EditorThemeTokens;
        // ImGui tab과 control은 FramePadding을 공유한다.
        static_assert(T::TabHeight == T::ControlHeight);
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
        style.FrameBorderSize = 0.f;
        style.ItemSpacing = ImVec2(T::ItemGapX, T::RowHeight - T::BodyFontSize);
        style.ItemInnerSpacing = ImVec2(T::PanelPaddingY, T::ItemGapY);
        style.CellPadding = ImVec2(T::PanelPaddingY, T::CompactGap);
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
        set(ImGuiCol_FrameBg, C::Canvas);
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
        set(ImGuiCol_CheckboxSelectedBg, C::Canvas);
        set(ImGuiCol_SliderGrab, C::Primary);
        set(ImGuiCol_SliderGrabActive, C::Primary);
        set(ImGuiCol_Button, C::Panel);
        set(ImGuiCol_ButtonHovered, C::PanelRaised);
        set(ImGuiCol_ButtonActive, C::Primary);
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
