#include "EditorThemeSelfTest.h"

#include "EditorTheme.h"
#include "EditorSectionHeader.h"
#include "ImGui.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace editor
{
    namespace
    {
        struct ThemeContractChecks
        {
            std::string& report;
            int checked{ 0 };
            int failed{ 0 };

            void expect(bool condition, std::string_view scenario, std::string_view property)
            {
                ++checked;
                if (condition) return;
                ++failed;
                report += "[FAIL] theme ";
                report += scenario;
                report += ": ";
                report += property;
                report += '\n';
            }

            void number(float actual, float expected,
                        std::string_view scenario, std::string_view property)
            {
                const bool equal = std::isfinite(actual) &&
                    std::fabs(actual - expected) < 0.0001f;
                expect(equal, scenario, property);
                if (!equal)
                {
                    report += "  expected=" + std::to_string(expected) +
                              " actual=" + std::to_string(actual) + '\n';
                }
            }

            void color(const ImVec4& actual, std::uint32_t rgb, float alpha,
                       std::string_view scenario, std::string_view property)
            {
                const auto byte_matches = [](float channel, std::uint32_t byte)
                {
                    return std::isfinite(channel) &&
                        std::fabs(channel * 255.f - static_cast<float>(byte)) < 0.001f;
                };
                expect(byte_matches(actual.x, (rgb >> 16) & 255) &&
                       byte_matches(actual.y, (rgb >> 8) & 255) &&
                       byte_matches(actual.z, rgb & 255) &&
                       std::isfinite(actual.w) && std::fabs(actual.w - alpha) < 0.0001f,
                       scenario, property);
            }
        };

        struct ThemeGeometryExpectation
        {
            const char* scenario;
            float user;
            float dpi;
            float scrollbar;
            float indent;
            float panel_x;
            float panel_y;
            float control_x;
            float control_y;
            float radius;
            float active_marker;
        };
    }

    bool RunEditorThemeSelfTest(std::string& report)
    {
        ThemeContractChecks checks{ report };

        // Plan §3.1 values are fixed independently of the product token table.
        struct ExpectedColor { ThemeColor token; const char* name; std::uint32_t rgb; };
        constexpr ExpectedColor palette[] = {
            { ThemeColor::Canvas, "Canvas", 0x181818 },
            { ThemeColor::Chrome, "Chrome", 0x2A2A2A },
            { ThemeColor::Panel, "Panel", 0x343434 },
            { ThemeColor::PanelRaised, "PanelRaised", 0x484848 },
            { ThemeColor::Selection, "Selection", 0x525252 },
            { ThemeColor::Border, "Border", 0x3E3E3E },
            { ThemeColor::BorderStrong, "BorderStrong", 0x484848 },
            { ThemeColor::Primary, "Primary", 0x2E70EA },
            { ThemeColor::Text, "Text", 0xFFFFFF },
            { ThemeColor::TextMuted, "TextMuted", 0x9E9E9E },
            { ThemeColor::TextDisabled, "TextDisabled", 0x999999 },
            { ThemeColor::Positive, "Positive", 0x5AEB5C },
            { ThemeColor::Warning, "Warning", 0xE6DB74 },
            { ThemeColor::Error, "Error", 0xFB5A5A },
        };
        checks.expect(static_cast<int>(ThemeColor::Count) == 14, "palette", "14 semantic colors");
        for (const ExpectedColor& expected : palette)
        {
            checks.expect(ThemeColorHex(expected.token) == expected.rgb,
                          "palette hex", expected.name);
            const char* actual_name = ThemeColorName(expected.token);
            checks.expect(actual_name && std::string_view(actual_name) == expected.name,
                          "palette name", expected.name);
            checks.color(ThemeColorValue(expected.token), expected.rgb, 1.f,
                         "palette rgba", expected.name);
            checks.color(ThemeColorValue(expected.token, 0.35f), expected.rgb, 0.35f,
                         "explicit alpha", expected.name);
        }

        ImGuiStyle style;
        style.FontSizeBase = 16.f;
        ApplyEditorTheme(style, 1.f, 1.f);
        const ImGuiStyle baseline = style;
        checks.number(style.FontSizeBase + style.ItemSpacing.y, 24.f,
                      "logical metrics", "row height");
        checks.number(style.FontSizeBase + 2.f * style.FramePadding.y, 24.f,
                      "logical metrics", "control and tab height");

        // Verify colors reach interaction states, not only the read-only token API.
        struct ExpectedSlot { ImGuiCol slot; const char* name; std::uint32_t rgb; float alpha; };
        constexpr ExpectedSlot slots[] = {
            { ImGuiCol_WindowBg, "canvas", 0x181818, 1.f },
            { ImGuiCol_TitleBgActive, "active title chrome", 0x2A2A2A, 1.f },
            { ImGuiCol_PopupBg, "popup panel", 0x343434, 1.f },
            { ImGuiCol_Button, "idle button", 0x343434, 1.f },
            { ImGuiCol_ButtonHovered, "hover button", 0x484848, 1.f },
            { ImGuiCol_ButtonActive, "pressed button", 0x2E70EA, 1.f },
            { ImGuiCol_FrameBgActive, "active field selection", 0x525252, 1.f },
            { ImGuiCol_Border, "panel border", 0x3E3E3E, 1.f },
            { ImGuiCol_TableBorderStrong, "strong border", 0x484848, 1.f },
            { ImGuiCol_Text, "body text", 0xFFFFFF, 1.f },
            { ImGuiCol_TextDisabled, "disabled text", 0x999999, 1.f },
            { ImGuiCol_PlotLines, "muted graph", 0x9E9E9E, 1.f },
            { ImGuiCol_PlotHistogramHovered, "positive graph", 0x5AEB5C, 1.f },
            { ImGuiCol_TabSelectedOverline, "active tab marker", 0x2E70EA, 1.f },
            { ImGuiCol_NavCursor, "keyboard focus", 0x2E70EA, 1.f },
            { ImGuiCol_TextSelectedBg, "text selection alpha", 0x2E70EA, 0.35f },
            { ImGuiCol_DockingPreview, "dock preview alpha", 0x2E70EA, 0.45f },
            { ImGuiCol_BorderShadow, "transparent shadow", 0x181818, 0.f },
        };
        for (const ExpectedSlot& expected : slots)
            checks.color(style.Colors[expected.slot], expected.rgb, expected.alpha,
                         "ImGui slot", expected.name);

        // Expected pixel values include ImGui's geometry truncation (7*1.5=10,
        // 6*2.25=13). The same object traverses both axes and repeated scales.
        constexpr ThemeGeometryExpectation transitions[] = {
            { "initial 100/100", 1.f, 1.f, 8.f, 20.f, 8.f, 6.f, 7.f, 4.f, 4.f, 2.f },
            { "user 150/100", 1.5f, 1.f, 12.f, 30.f, 12.f, 9.f, 10.f, 6.f, 6.f, 3.f },
            { "user return 100/100", 1.f, 1.f, 8.f, 20.f, 8.f, 6.f, 7.f, 4.f, 4.f, 2.f },
            { "dpi 100/150", 1.f, 1.5f, 12.f, 30.f, 12.f, 9.f, 10.f, 6.f, 6.f, 3.f },
            { "dpi return 100/100", 1.f, 1.f, 8.f, 20.f, 8.f, 6.f, 7.f, 4.f, 4.f, 2.f },
            { "combined 150/150", 1.5f, 1.5f, 18.f, 45.f, 18.f, 13.f, 15.f, 9.f, 9.f, 4.f },
            { "repeat combined 150/150", 1.5f, 1.5f, 18.f, 45.f, 18.f, 13.f, 15.f, 9.f, 9.f, 4.f },
            { "combined return 100/100", 1.f, 1.f, 8.f, 20.f, 8.f, 6.f, 7.f, 4.f, 4.f, 2.f },
        };
        for (const ThemeGeometryExpectation& expected : transitions)
        {
            ApplyEditorTheme(style, expected.user, expected.dpi);
            checks.number(style.FontScaleMain, expected.user, expected.scenario, "user font axis");
            checks.number(style.FontScaleDpi, expected.dpi, expected.scenario, "DPI font axis");
            checks.number(style.FontSizeBase, 16.f, expected.scenario, "font base stays unscaled");
            checks.number(style.ScrollbarSize, expected.scrollbar, expected.scenario, "scrollbar width");
            checks.number(style.IndentSpacing, expected.indent, expected.scenario, "tree indentation");
            checks.number(style.WindowPadding.x, expected.panel_x, expected.scenario, "panel padding x");
            checks.number(style.WindowPadding.y, expected.panel_y, expected.scenario, "panel padding y");
            checks.number(style.FramePadding.x, expected.control_x, expected.scenario, "control padding x");
            checks.number(style.FramePadding.y, expected.control_y, expected.scenario, "control padding y");
            checks.number(style.FrameRounding, expected.radius, expected.scenario, "control radius");
            checks.number(style.TabBarOverlineSize, expected.active_marker, expected.scenario, "active tab marker");
            bool same_colors = true;
            for (int slot = 0; slot < ImGuiCol_COUNT; ++slot)
            {
                const ImVec4& color = style.Colors[slot];
                const ImVec4& original = baseline.Colors[slot];
                same_colors = same_colors && color.x == original.x && color.y == original.y &&
                    color.z == original.z && color.w == original.w;
            }
            checks.expect(same_colors, expected.scenario, "scaling preserves every color");
        }

        // A saved/scaled style is not a new baseline. Also reset fields outside
        // the explicit theme mapping, while retaining the font system's base size.
        style.Alpha = 0.2f;
        style.WindowPadding = ImVec2(700.f, 900.f);
        style.WindowRounding = 99.f;
        style.ScrollbarSize = 123.f;
        style.IndentSpacing = 987.f;
        style.FontScaleMain = 7.f;
        style.FontScaleDpi = 9.f;
        style.FontSizeBase = 19.f;
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.1f, 0.2f, 0.3f, 0.4f);
        ApplyEditorTheme(style, 1.f, 1.f);
        checks.number(style.Alpha, 1.f, "recovery", "default opacity");
        checks.number(style.WindowPadding.x, 8.f, "recovery", "panel padding x");
        checks.number(style.WindowPadding.y, 6.f, "recovery", "panel padding y");
        checks.number(style.WindowRounding, 0.f, "recovery", "square window");
        checks.number(style.ScrollbarSize, 8.f, "recovery", "scrollbar width");
        checks.number(style.IndentSpacing, 20.f, "recovery", "tree indentation");
        checks.number(style.FontScaleMain, 1.f, "recovery", "user font axis");
        checks.number(style.FontScaleDpi, 1.f, "recovery", "DPI font axis");
        checks.number(style.FontSizeBase, 19.f, "recovery", "font owner keeps base size");
        checks.color(style.Colors[ImGuiCol_ButtonActive], 0x2E70EA, 1.f,
                     "recovery", "active button palette");

        // 섹션 머리줄의 상태 색 넷 (PHASE 21 W2).
        //
        // 이 단정이 없으면 누가 `Panel` 과 `PanelRaised` 를 같은 값으로 만들어도
        // 조용히 지나간다 — 머리줄은 열림/닫힘을 배경색으로만 알리므로 상태
        // 표시가 통째로 사라지는데 빌드도 다른 검사도 붉어지지 않는다. 그리고
        // 그 값을 Header 계열 style 칸에서 읽지 않는 이유가 바로 테마가
        // `HeaderHovered` 와 `HeaderActive` 를 같은 값으로 두었기 때문이라,
        // 같은 충돌이 다시 생기는 것을 막아야 한다.
        {
            using surface = widgets::section_header_surface;
            constexpr std::array<surface, 4> surfaces{
                surface::Closed, surface::Open, surface::Hovered, surface::Held };
            constexpr std::array<const char*, 4> names{
                "closed", "open", "hovered", "held" };
            constexpr std::array<std::uint32_t, 4> expected{
                0x343434, 0x484848, 0x525252, 0x2E70EA };

            for (std::size_t index = 0; index < surfaces.size(); ++index)
            {
                checks.expect(
                    widgets::section_header_surface_hex(surfaces[index]) == expected[index],
                    "section header", names[index]);
            }
            for (std::size_t left = 0; left < surfaces.size(); ++left)
            {
                for (std::size_t right = left + 1; right < surfaces.size(); ++right)
                {
                    checks.expect(
                        widgets::section_header_surface_hex(surfaces[left]) !=
                        widgets::section_header_surface_hex(surfaces[right]),
                        "section header",
                        "states differ");
                }
            }
        }

        report += "[";
        report += checks.failed == 0 ? "OK" : "FAIL";
        report += "] editor theme contracts: " + std::to_string(checks.checked) +
                  " checks, " + std::to_string(checks.failed) + " failures\n";
        return checks.failed == 0;
    }
}
