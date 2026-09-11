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

    // PHASE 21 §3.1: S&Box theme.json을 참고한 2026-09-11 이식값.
    // 외부 파일을 읽지 않는다. 아래 geometry는 배율 적용 전 logical px이다.
    struct EditorThemeTokens
    {
        static constexpr float RowHeight = 24.f;
        static constexpr float ControlHeight = 24.f;
        static constexpr float ControlRadius = 4.f;
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
        static constexpr float PanelPaddingX = 8.f;
        static constexpr float PanelPaddingY = 6.f;
        static constexpr float ControlPaddingX = 7.f;
        static constexpr float ItemGapX = 8.f;
        static constexpr float ItemGapY = 4.f;
        static constexpr float CompactGap = 2.f;
        static constexpr float PropertyGapX = 1.f;
    };

    // RGB hex 관측과 실제 ImGui 색이 같은 표를 읽는다.
    std::uint32_t ThemeColorHex(ThemeColor color) noexcept;
    const char* ThemeColorName(ThemeColor color) noexcept;
    ImVec4 ThemeColorValue(ThemeColor color, float alpha = 1.f) noexcept;

    // 창 본문에서 지정하는 geometry도 style과 같은 두 축을 한 번만 적용한다.
    float ThemePixels(float logicalPixels) noexcept;

    // 이전 배율이 적용된 style을 재사용하지 않고 기준값부터 다시 세운다.
    // 폰트는 FontScaleMain(user), FontScaleDpi(monitor)가 각각 소유한다.
    void ApplyEditorTheme(ImGuiStyle& style, float userScale, float dpiScale) noexcept;
}
