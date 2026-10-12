#pragma once

#include <cstdint>

struct ImGuiStyle;
struct ImVec4;

namespace editor
{
    enum class ThemeColor
    {
        Canvas, Chrome, Panel, PanelRaised, Selection, Border, BorderStrong,
        Primary, Text, TextMuted, TextDisabled, Positive, Warning, Error, Count
    };

    // Compact editor geometry with Spectrum-inspired neutral surfaces.
    // Values are logical pixels before user and monitor scale are applied.
    struct EditorThemeTokens
    {
        static constexpr float RowHeight = 20.f;
        static constexpr float ControlHeight = 20.f;
        static constexpr float ControlRadius = 3.f;
        static constexpr float TabHeight = 24.f;
        static constexpr float TabActiveMarker = 2.f;
        static constexpr float TreeIndent = 20.f;
        static constexpr float ScrollbarWidth = 8.f;
        static constexpr float PanelGap = 1.f;
        static constexpr float BodyFontSize = 16.f;
        static constexpr float IconFontSize = 16.f;
        static constexpr float IconBaselineOffset = 0.f;
        static constexpr float SmallFontSize = 12.f;
        static constexpr float ExtraSmallFontSize = 10.f;
        // Minimum title-row height in final framebuffer pixels, after scaling.
        static constexpr float TitleBarHeight = 45.f;
        static constexpr float TitleBarFontSize = 12.f;
        static constexpr float MenuGapX = 10.f;
        static constexpr float MenuGapY = 6.f;
        static constexpr float MenuPaddingX = 8.f;
        static constexpr float MenuPaddingY = 6.f;
        static constexpr float PanelPaddingX = 3.f;
        static constexpr float PanelPaddingY = 2.f;
        static constexpr float ItemInnerGapX = 6.f;
        static constexpr float CellPaddingX = 6.f;
        static constexpr float ControlPaddingX = 7.f;
        static constexpr float ItemGapX = 8.f;
        static constexpr float ItemGapY = 4.f;
        static constexpr float CompactGap = 2.f;
        static constexpr float PropertyGapX = 1.f;
    };

    // Inspector surfaces share the workspace's flat neutral palette.
    // Separate from the workspace canvas so viewport contrast is unchanged.
    struct InspectorThemeTokens
    {
        static constexpr float PanelPaddingX = 10.f;
        static constexpr float BackgroundR = 43.f / 255.f;
        static constexpr float BackgroundG = 43.f / 255.f;
        static constexpr float BackgroundB = 43.f / 255.f;
        static constexpr std::uint32_t Field = 0x1E1E1E;
        static constexpr std::uint32_t Label = 0xB8B8B8;
        static constexpr std::uint32_t AxisBackground = 0x1E1E1E;
        static constexpr float RowGap = 3.f;
        static constexpr float AxisGap = 3.f;
    };

    // Keep window/tab handles independent of compact body controls. This scope
    // also surrounds NewFrame, which draws floating dock-node tabs.
    class TabStyleScope
    {
    public:
        TabStyleScope();
        ~TabStyleScope();
        TabStyleScope(const TabStyleScope&) = delete;
        TabStyleScope& operator=(const TabStyleScope&) = delete;
    private:
        float m_paddingY;
    };

    // Body style is balanced even when the Inspector returns early.
    class InspectorStyleScope
    {
    public:
        InspectorStyleScope();
        ~InspectorStyleScope();
        InspectorStyleScope(const InspectorStyleScope&) = delete;
        InspectorStyleScope& operator=(const InspectorStyleScope&) = delete;
    };
    bool InspectorStyleActive() noexcept;

    // Hierarchy reference palette. The tree stays darker than its surrounding panel.
    struct HierarchyThemeTokens
    {
        static constexpr float BackgroundR = 43.f / 255.f;
        static constexpr float BackgroundG = 43.f / 255.f;
        static constexpr float BackgroundB = 43.f / 255.f;
        static constexpr std::uint32_t Row = 0x1E1E1E;
        static constexpr std::uint32_t AlternateRow = 0x232323;
        static constexpr std::uint32_t SelectedRow = 0x373737;
        static constexpr std::uint32_t SelectedHoveredRow = 0x404040;
        static constexpr std::uint32_t EntityText = 0xD6D6D6;
        static constexpr std::uint32_t SelectedText = 0xF0F0F0;
        static constexpr float ToolbarGap = 3.f;
    };

    class HierarchyStyleScope
    {
    public:
        HierarchyStyleScope();
        ~HierarchyStyleScope();
        HierarchyStyleScope(const HierarchyStyleScope&) = delete;
        HierarchyStyleScope& operator=(const HierarchyStyleScope&) = delete;
    };

    // RGB hex 관측과 실제 ImGui 색이 같은 표를 읽는다.
    std::uint32_t ThemeColorHex(ThemeColor color) noexcept;
    const char* ThemeColorName(ThemeColor color) noexcept;
    ImVec4 ThemeColorValue(ThemeColor color, float alpha = 1.f) noexcept;

    // 창 본문에서 지정하는 geometry도 style과 같은 두 축을 한 번만 적용한다.
    float ThemePixels(float logicalPixels) noexcept;

    // Font and safe-area padding already include user/monitor scale. The 45px
    // floor is physical; framebufferScaleY only converts it to ImGui coordinates.
    float TitleBarFramePaddingY(float fontHeight, float safePaddingY, float framebufferScaleY = 1.f) noexcept;

    // 이전 배율이 적용된 style을 재사용하지 않고 기준값부터 다시 세운다.
    // 폰트는 FontScaleMain(user), FontScaleDpi(monitor)가 각각 소유한다.
    void ApplyEditorTheme(ImGuiStyle& style, float userScale, float dpiScale) noexcept;
}
