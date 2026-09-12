#include "EditorThemeSelfTest.h"

#include "EditorTheme.h"
#include "EditorSectionHeader.h"
#include "EditorPropertyRow.h"
#include "EditorAxisField3.h"
#include "EditorModeButton.h"
#include "ImGui.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace editor
{
    namespace
    {
        // WCAG 상대 휘도로 두 색의 대비를 잰다. 축 badge 위의 글자가 읽히는지를
        // 눈이 아니라 숫자로 판정하기 위해서다.
        float channel_luminance(std::uint32_t byte) noexcept
        {
            const float value = static_cast<float>(byte) / 255.f;
            return value <= 0.03928f ? value / 12.92f
                                     : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float relative_luminance(std::uint32_t rgb) noexcept
        {
            return 0.2126f * channel_luminance((rgb >> 16) & 255) +
                   0.7152f * channel_luminance((rgb >> 8) & 255) +
                   0.0722f * channel_luminance(rgb & 255);
        }

        float contrast_ratio(std::uint32_t left, std::uint32_t right) noexcept
        {
            const float a = relative_luminance(left);
            const float b = relative_luminance(right);
            const float bright = a > b ? a : b;
            const float dark = a > b ? b : a;
            return (bright + 0.05f) / (dark + 0.05f);
        }

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


        // 속성 줄의 필드 ID (PHASE 21 W2).
        //
        // 원본(`TableAPIHelper.h::DrawVec2Row`)은 `"##x"`·`"##y"` 두 개가 몸통에
        // 박혀 있었다. 열 개수를 인자로 올리면서 표로 바뀌었는데, 그 표에 같은
        // 이름이 두 번 들어가면 두 칸이 한 ImGui ID 를 나눠 갖는다 — 한 칸을
        // 드래그하면 다른 칸이 함께 움직이고, 빌드도 다른 검사도 붉어지지 않는다.
        {
            const int limit = widgets::property_row_field_limit();
            checks.expect(limit >= 3, "property row", "holds at least a vec3");
            for (int index = 0; index < limit; ++index)
            {
                const char* const id = widgets::property_row_field_id(index);
                checks.expect(nullptr != id && '#' == id[0] && '#' == id[1],
                              "property row", "field id stays hidden");
                for (int other = index + 1; other < limit; ++other)
                {
                    const char* const rival = widgets::property_row_field_id(other);
                    checks.expect(nullptr != id && nullptr != rival &&
                                  0 != std::strcmp(id, rival),
                                  "property row", "field ids differ");
                }
            }
            checks.expect(nullptr == widgets::property_row_field_id(-1) &&
                          nullptr == widgets::property_row_field_id(limit),
                          "property row", "field id refuses out of range");
        }

        // 축 badge 의 색 (PHASE 21 W2).
        //
        // 단정 셋이 서로 다른 것을 지킨다. ① 세 축이 서로 다를 것. ② 축 색이
        // 팔레트의 의미 색(Error·Positive·Primary)과 다를 것 — 같아지면 이 줄에
        // error 상태를 넣는 순간 X 축과 색이 겹쳐 상태 표시가 사라진다.
        // ③ badge 위 흰 글자가 읽힐 것. 밝은 초록(`Positive`)을 그대로 쓰면
        // 대비가 2.1:1 이라 글자가 배경에 묻는다.
        {
            using axis = widgets::axis;
            constexpr std::array<axis, 3> axes{ axis::X, axis::Y, axis::Z };
            constexpr std::array<const char*, 3> axis_names{ "axis x", "axis y", "axis z" };
            constexpr std::array<std::uint32_t, 3> axis_expected{
                0xC0392B, 0x4F7A28, 0x2D6FA8 };
            constexpr std::array<ThemeColor, 3> meaning{
                ThemeColor::Error, ThemeColor::Positive, ThemeColor::Primary };

            for (std::size_t index = 0; index < axes.size(); ++index)
            {
                const std::uint32_t badge = widgets::axis_badge_hex(axes[index]);
                checks.expect(badge == axis_expected[index], axis_names[index], "badge color");
                checks.expect(nullptr != widgets::axis_badge_label(axes[index]),
                              axis_names[index], "badge carries a letter");
                for (const ThemeColor token : meaning)
                {
                    checks.expect(badge != ThemeColorHex(token),
                                  axis_names[index], "badge is not a meaning color");
                }
                checks.expect(
                    contrast_ratio(badge, widgets::axis_badge_text_hex()) >= 3.f,
                    axis_names[index], "badge letter stays readable");
            }
            for (std::size_t left = 0; left < axes.size(); ++left)
            {
                for (std::size_t right = left + 1; right < axes.size(); ++right)
                {
                    checks.expect(widgets::axis_badge_hex(axes[left]) !=
                                  widgets::axis_badge_hex(axes[right]),
                                  "axis badge", "axes differ");
                }
            }
            checks.expect(0u == widgets::axis_badge_hex(axis::Count) &&
                          nullptr == widgets::axis_badge_label(axis::Count),
                          "axis badge", "refuses out of range");
        }

        // 모드 버튼의 표면과 marker (PHASE 21 W2).
        //
        // 켜짐은 배경이 아니라 아래 marker 가 든다. marker 가 배경 셋 중 하나와
        // 같은 값이 되면 켜짐 표시가 조용히 사라진다 — 원본이 세 style 칸을 모두
        // 같은 색으로 덮어 hover 와 press 를 잃었던 것과 같은 충돌이다.
        {
            using surface = widgets::mode_button_surface;
            constexpr std::array<surface, 3> surfaces{
                surface::Idle, surface::Hovered, surface::Held };
            constexpr std::array<const char*, 3> names{ "idle", "hovered", "held" };
            constexpr std::array<std::uint32_t, 3> expected{
                0x2A2A2A, 0x484848, 0x525252 };

            for (std::size_t index = 0; index < surfaces.size(); ++index)
            {
                checks.expect(
                    widgets::mode_button_surface_hex(surfaces[index]) == expected[index],
                    "mode button", names[index]);
            }
            for (std::size_t left = 0; left < surfaces.size(); ++left)
            {
                for (std::size_t right = left + 1; right < surfaces.size(); ++right)
                {
                    checks.expect(
                        widgets::mode_button_surface_hex(surfaces[left]) !=
                        widgets::mode_button_surface_hex(surfaces[right]),
                        "mode button", "surfaces differ");
                }
                checks.expect(
                    widgets::mode_button_surface_hex(surfaces[left]) !=
                    widgets::mode_button_marker_hex(),
                    "mode button", "marker stands out from every surface");
            }
            checks.expect(widgets::mode_button_text_hex(true) !=
                          widgets::mode_button_text_hex(false),
                          "mode button", "disabled icon reads differently");
        }

        // ── 배치 계약 (W2-I2) ────────────────────────────────────────
        //
        // `measure_property_layout` 은 ImGui 를 부르지 않는 순수 함수라 여기서
        // 합성 폭으로 직접 몰 수 있다. 화면 캡처로는 경계 왕복과 편집 중 보류를
        // 잴 수 없어서 그렇게 뗐다.
        {
            using widgets::property_layout_inputs;
            using widgets::property_layout_mode;
            using widgets::property_layout_state;

            // 계획서 §7 의 검증 폭 넷과 같은 단위다. 치수는 100% 배율 기준의
            // 대표값을 손으로 넣는다 — 폰트에서 재면 검사가 폰트에 묶인다.
            const auto make = [](float available) {
                property_layout_inputs in{};
                in.available = available;
                in.label_max = 160.f;
                in.label_ratio = widgets::property_layout_label_ratio();
                in.value_min = 60.f;
                in.axis_value_min = 40.f;
                in.badge_width = 12.f;
                in.gap = 8.f;
                in.axis_gap = 4.f;
                in.hysteresis = 8.f;
                return in;
            };

            // ① 라벨 열은 상한을 넘지 않는다. 720px 의 40% 는 288 이다.
            {
                property_layout_state state{};
                const auto metrics = widgets::measure_property_layout(make(720.f), state);
                checks.expect(metrics.label_col <= 160.f + 0.01f,
                    "property layout", "label column honours its cap");
                checks.expect(metrics.value_col > 480.f,
                    "property layout", "surplus width goes to the value column");
            }

            // ② 좁으면 값이 라벨 아래로 내려간다.
            {
                property_layout_state state{};
                const auto wide = widgets::measure_property_layout(make(480.f), state);
                checks.expect(property_layout_mode::inline_row == wide.mode,
                    "property layout", "480px stays inline");

                const auto narrow = widgets::measure_property_layout(make(100.f), state);
                checks.expect(property_layout_mode::stacked == narrow.mode,
                    "property layout", "100px stacks the value under the label");
                checks.expect(narrow.label_col >= narrow.value_col - 0.01f,
                    "property layout", "stacked label spans the row");
            }

            // ③ 완충 폭이 경계 왕복을 막는다. stacked 로 넘어간 임계값으로
            //    되돌려도 inline 으로 돌아오지 않아야 한다 — 이것이 없으면
            //    한 픽셀 흔들림에 열 위치가 매 프레임 바뀐다.
            {
                property_layout_state state{};
                // inline → stacked 로 넘어가는 경계를 찾는다.
                float boundary = 0.f;
                for (float w = 400.f; w > 20.f; w -= 1.f)
                {
                    const auto m = widgets::measure_property_layout(make(w), state);
                    if (property_layout_mode::stacked == m.mode) { boundary = w; break; }
                }
                checks.expect(boundary > 0.f,
                    "property layout", "a stacking boundary exists");

                // 경계 바로 위로 되돌린다. 완충 폭만큼 더 넓어지기 전에는
                // stacked 를 유지해야 한다.
                const auto back = widgets::measure_property_layout(make(boundary + 1.f), state);
                checks.expect(property_layout_mode::stacked == back.mode,
                    "property layout", "hysteresis holds the stacked mode at the boundary");

                const auto recovered =
                    widgets::measure_property_layout(make(boundary + 40.f), state);
                checks.expect(property_layout_mode::inline_row == recovered.mode,
                    "property layout", "enough extra width returns to inline");
            }

            // ④ 편집 중에는 모드를 붙든다. 드래그 중 값 칸이 다른 줄로 옮겨
            //    가면 드래그가 끊긴다.
            {
                property_layout_state state{};
                widgets::measure_property_layout(make(480.f), state);
                property_layout_inputs editing = make(60.f);
                editing.editing = true;
                const auto held = widgets::measure_property_layout(editing, state);
                checks.expect(property_layout_mode::inline_row == held.mode,
                    "property layout", "editing holds the previous mode");
                checks.expect(!held.axis_stacked,
                    "property layout", "editing holds the previous axis mode");
            }

            // ⑤ 축은 값 열을 셋으로 나눈 뒤에 판정한다. 슬롯이 badge + 숫자
            //    최소 폭을 못 담으면 세로로 간다.
            {
                property_layout_state state{};
                const auto wide = widgets::measure_property_layout(make(720.f), state);
                checks.expect(!wide.axis_stacked,
                    "property layout", "720px keeps the three axes on one line");

                const auto narrow = widgets::measure_property_layout(make(240.f), state);
                checks.expect(narrow.axis_stacked,
                    "property layout", "240px stacks the axes vertically");
            }

            // ⑥ 라벨 힌트가 열의 위를 한 번 더 막는다. 힌트가 없으면 짧은
            //    이름만 있는 구간도 가용 폭의 40% 를 통째로 가져가고, 그만큼
            //    값 열이 줄어 축이 불필요하게 세로로 떨어진다.
            {
                property_layout_state state{};
                property_layout_inputs hinted = make(720.f);
                hinted.label_hint = 40.f;
                const auto metrics = widgets::measure_property_layout(hinted, state);
                checks.expect(metrics.label_col <= 40.01f,
                    "property layout", "label hint caps the column");
                checks.expect(metrics.value_col > 660.f,
                    "property layout", "the width the hint saves goes to the value column");
                checks.expect(!metrics.axis_stacked,
                    "property layout", "the saved width keeps the axes inline");
            }

            // ⑦ 축 최소 폭은 행 전체의 것과 다른 값이다. 같은 잣대를 대면
            //    셋이 한 줄을 나눠 쓰는 축은 넉넉한 폭에서도 세로로 떨어진다.
            //    실제로 처음 판이 그랬다.
            {
                property_layout_state state{};
                // 두 잣대 사이에 슬롯이 떨어지는 폭을 고른다. 그래야 잣대가
                // 갈리는 것만으로 판정이 뒤집히는 것을 보인다.
                property_layout_inputs coarse = make(330.f);
                coarse.axis_value_min = coarse.value_min; // 행 전체의 잣대를 축에 댄다
                const auto wrong = widgets::measure_property_layout(coarse, state);

                property_layout_state fresh{};
                const auto right = widgets::measure_property_layout(make(330.f), fresh);
                checks.expect(wrong.axis_stacked && !right.axis_stacked,
                    "property layout", "axis minimum is not the row minimum");

                const char* axisSample = widgets::property_layout_axis_sample();
                checks.expect(nullptr != axisSample &&
                    0 != std::strcmp(axisSample, widgets::property_layout_value_sample()),
                    "property layout", "axis sample differs from the row sample");
            }

            // ⑧ 값 최소 폭은 현재 숫자가 아니라 고정 대표 문자열로 잰다.
            //    이것이 흔들리면 값이 바뀔 때마다 열이 움직인다.
            {
                const char* sample = widgets::property_layout_value_sample();
                checks.expect(nullptr != sample && '\0' != sample[0],
                    "property layout", "value sample is a fixed string");
                checks.expect(widgets::property_layout_label_max_logical() > 0.f,
                    "property layout", "label cap is a positive logical pixel value");
                checks.expect(widgets::property_layout_label_ratio() > 0.f &&
                    widgets::property_layout_label_ratio() < 1.f,
                    "property layout", "label ratio is a proper fraction");
            }
        }

        report += "[";
        report += checks.failed == 0 ? "OK" : "FAIL";
        report += "] editor theme contracts: " + std::to_string(checks.checked) +
                  " checks, " + std::to_string(checks.failed) + " failures\n";
        return checks.failed == 0;
    }
}
