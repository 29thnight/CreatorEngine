#include "EditorInspectorPanel.h"
#include "EditorNavContract.h"
#include "EditorClipContract.h"

#include "ImGui.h"
#include "EditorTheme.h"

namespace editor::widgets
{
    namespace
    {
        // s&box `InspectorHeader.BuildUI` 의 칸 폭. 논리 픽셀이다.
        constexpr float kLeadLogical      = 4.f;
        constexpr float kExpanderLogical  = 16.f;
        constexpr float kIconLogical      = 22.f;
        constexpr float kIconGapLogical   = 4.f;
        constexpr float kToggleLogical    = 16.f;
        constexpr float kToggleGapLogical = 6.f;
        constexpr float kButtonLogical    = 20.f;
        constexpr float kButtonGapLogical = 2.f;
        constexpr float kTrailLogical     = 16.f;

        // `FixedHeight = Theme.RowHeight + 8`.
        constexpr float kHeightPadLogical = 8.f;

        // 위쪽 경계선 두 줄의 두께. 논리 픽셀이 아니라 **물리 1px 고정**이다 —
        // 배율을 곱하면 DPI 2 에서 4px 홈이 되어 선이 아니라 띠가 된다.
        constexpr float kBorderThickness = 1.f;

        // hover 시 머리줄에 얹는 강조색의 알파. s&box 의
        // `Theme.Blue.WithAlpha( 0.1f )` 그대로다.
        constexpr float kHoverAlpha = 0.10f;

        // 접힘·비활성 불투명도. s&box `OnPaint` 의 두 값이다.
        constexpr float kCollapsedOpacity = 0.8f;
        constexpr float kDisabledOpacity  = 0.7f;

        ImU32 token_color(ThemeColor color, float alpha) noexcept
        {
            return ImGui::GetColorU32(ThemeColorValue(color, alpha));
        }
    }

    inspector_panel_metrics measure_inspector_panel(
        const inspector_panel_inputs& inputs) noexcept
    {
        const float scale = inputs.scale > 0.f ? inputs.scale : 1.f;

        inspector_panel_metrics metrics{};
        metrics.height     = inputs.row_height + kHeightPadLogical * scale;
        metrics.lead       = kLeadLogical * scale;
        metrics.expander   = kExpanderLogical * scale;
        metrics.icon       = kIconLogical * scale;
        metrics.icon_gap   = kIconGapLogical * scale;
        metrics.toggle     = kToggleLogical * scale;
        metrics.toggle_gap = kToggleGapLogical * scale;
        metrics.button     = kButtonLogical * scale;
        metrics.button_gap = kButtonGapLogical * scale;
        metrics.trail      = kTrailLogical * scale;

        // 체크박스 칸은 **언제나** 더한다. 있고 없고로 이름 x 가 달라지면
        // 활성 토글이 없는 Transform 만 이름이 왼쪽으로 밀린다.
        metrics.title_x = metrics.lead + metrics.expander + metrics.icon +
            metrics.icon_gap + metrics.toggle + metrics.toggle_gap;

        const int buttons = inputs.button_count > 0 ? inputs.button_count : 0;
        const float right = (buttons > 0)
            ? (metrics.button * static_cast<float>(buttons) +
               metrics.button_gap * static_cast<float>(buttons - 1))
            : 0.f;

        metrics.title_w = ImMax(
            inputs.available - metrics.title_x - right - metrics.trail, 1.f);
        return metrics;
    }

    float inspector_panel_opacity(bool expanded, bool disabled) noexcept
    {
        float opacity = 1.f;
        if (!expanded)
        {
            opacity = kCollapsedOpacity;
        }
        if (disabled)
        {
            opacity = ImMin(opacity, kDisabledOpacity);
        }
        return opacity;
    }

    inspector_panel_result begin_inspector_panel(const inspector_panel_request& request)
    {
        inspector_panel_result result{};

        IM_ASSERT(nullptr != request.label && "패널은 이름이 ID 의 씨앗이다");
        if (nullptr == request.label)
        {
            return result;
        }

        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            // 여는 쪽이 ID 를 밀지 않았으니 닫는 쪽도 뺄 것이 없다. 이 경우를
            // 위해 `end_` 는 자기 상태를 보고 움직인다.
            ImGui::PushID(request.label);
            return result;
        }

        ImGui::PushID(request.label);
        const ImGuiID id = ImGui::GetID("##Panel");

        const ImGuiStyle& style = ImGui::GetStyle();

        inspector_panel_inputs inputs{};
        inputs.available = ImGui::GetContentRegionAvail().x;
        inputs.row_height = ImGui::GetFrameHeight();
        inputs.scale = ThemePixels(1.f);
        inputs.button_count = (nullptr != request.menu_icon) ? 1 : 0;
        const inspector_panel_metrics metrics = measure_inspector_panel(inputs);

        // ── 위쪽 경계선 두 줄 ────────────────────────────────────────────
        //
        // 어두운 줄 위, 밝은 줄 아래. 둘이 붙어 패인 홈처럼 보이고 그 홈이
        // 패널의 경계다. 머리줄보다 **먼저** 자리를 잡아야 앞 패널의 본문과
        // 이 패널 사이에 선이 선다.
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* const draw = ImGui::GetWindowDrawList();
            const float width = inputs.available;
            draw->AddRectFilled(origin,
                ImVec2(origin.x + width, origin.y + kBorderThickness),
                token_color(ThemeColor::Canvas, 1.f));
            draw->AddRectFilled(ImVec2(origin.x, origin.y + kBorderThickness),
                ImVec2(origin.x + width, origin.y + kBorderThickness * 2.f),
                token_color(ThemeColor::Border, 1.f));
            ImGui::Dummy(ImVec2(width, kBorderThickness * 2.f));
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImRect header_rect(origin,
            ImVec2(origin.x + inputs.available, origin.y + metrics.height));

        // 클릭 영역은 체크박스와 오른쪽 버튼을 **비켜서** 잡는다. 겹치지
        // 않으므로 아래에서 자리를 먼저 잡아도 그 둘의 클릭을 빼앗지 않는다.
        const ImRect click_zone(
            ImVec2(origin.x + metrics.lead, origin.y),
            ImVec2(origin.x + metrics.title_x + metrics.title_w,
                   origin.y + metrics.height));

        bool open    = ImGui::GetStateStorage()->GetBool(id, true);
        bool hovered = false;
        bool held    = false;

        const float toggleLeft = origin.x + metrics.lead + metrics.expander + metrics.icon + metrics.icon_gap;
        const float toggleTop = origin.y + (metrics.height - metrics.toggle) * .5f;
        const bool overToggle = request.enabled && ImGui::IsMouseHoveringRect(
            {toggleLeft, toggleTop}, {toggleLeft + metrics.toggle, toggleTop + metrics.toggle});
        ImGui::ItemSize(header_rect);
        if (ImGui::ItemAdd(click_zone, id))
        {
            ::editor::nav::announce_item("EditorInspectorPanel", id, true);
            // 이 위젯은 배경을 칠하지 않고 위쪽에 선 두 줄만 긋는다. 그래서
            // 커서를 어느 자리에 그려도 덮이지 않는다 — 신고 직후에 둔다.
            ::editor::nav::draw_cursor(click_zone, id, "EditorInspectorPanel");
            if (!overToggle && ImGui::ButtonBehavior(click_zone, id, &hovered, &held,
                    ImGuiButtonFlags_PressedOnClick))
            {
                if (request.collapsible)
                {
                    open = !open;
                    ImGui::GetStateStorage()->SetBool(id, open);
                }
            }
        }
        if (!request.collapsible)
        {
            open = true;
        }

        const bool disabled = (nullptr != request.enabled) && !*request.enabled;
        const float opacity = inspector_panel_opacity(open, disabled);

        // ── hover ────────────────────────────────────────────────────────
        //
        // 배경을 칠하는 유일한 자리다. 평소에는 아무것도 안 칠한다 — 칠하는
        // 순간 목록이 줄무늬가 되고, 그것이 `EditorSectionHeader` 를 여기 쓰지
        // 않는 이유다.
        if (hovered && request.collapsible)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(header_rect.Min, header_rect.Max,
                token_color(ThemeColor::Primary, kHoverAlpha));
        }

        const ImU32 tint = request.enabled ? token_color(ThemeColor::Primary, opacity) :
            ImGui::GetColorU32(ImGuiCol_TextDisabled, opacity);
        const float center_y = origin.y + metrics.height * 0.5f;

        // ── 쉐브론 ───────────────────────────────────────────────────────
        //
        // 글리프가 아니라 삼각형이다. 아이콘 폰트에 글리프가 빠져 있어도
        // 조용히 네모가 그려지지 않는다 — 이 저장소가 이미 겪은 실패 양식이라
        // 접힘 표시만은 폰트에 기대지 않는다.
        if (request.collapsible)
        {
            const float arrow_size = ImGui::GetFontSize() * 0.65f;
            ImGui::RenderArrow(ImGui::GetWindowDrawList(),
                ImVec2(origin.x + metrics.lead +
                           (metrics.expander - arrow_size) * 0.5f,
                       center_y - arrow_size * 0.5f),
                ImGui::GetColorU32(ImGuiCol_TextDisabled, opacity),
                open ? ImGuiDir_Down : ImGuiDir_Right, 0.65f);
        }

        // ── 아이콘 ───────────────────────────────────────────────────────
        if (nullptr != request.icon && '\0' != request.icon[0])
        {
            const ImVec2 size = ImGui::CalcTextSize(request.icon);
            const float icon_x = origin.x + metrics.lead + metrics.expander;
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(icon_x + (metrics.icon - size.x) * 0.5f,
                       center_y - size.y * 0.5f),
                tint, request.icon);
        }

        // ── 활성 체크박스 ────────────────────────────────────────────────
        if (nullptr != request.enabled)
        {
            const bool before = *request.enabled;
            // Checkbox uses GetFrameHeight for its square. Remove only its
            // vertical padding so the square follows the 16px body font.
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0.f));
            const float box = ImGui::GetFrameHeight();
            ImGui::SetCursorScreenPos(ImVec2(
                origin.x + metrics.lead + metrics.expander + metrics.icon +
                    metrics.icon_gap + (metrics.toggle - box) * 0.5f,
                center_y - box * 0.5f));
            ImGui::PushID("enabled");
            ImGui::Checkbox("##enabled", request.enabled);
            ImGui::PopID();
            ImGui::PopStyleVar();
            result.enabled_changed = (before != *request.enabled);
        }

        // ── 이름 ─────────────────────────────────────────────────────────
        {
            const char* const end = ImGui::FindRenderedTextEnd(request.label);
            const ImVec2 size = ImGui::CalcTextSize(request.label, end);
            const ImVec2 text_pos(origin.x + metrics.title_x,
                center_y - size.y * 0.5f);

            // 이름이 칸보다 길면 잘라 그린다. 오른쪽 버튼 위로 넘어가면 글자와
            // 아이콘이 겹쳐 둘 다 안 읽힌다.
            //
            // W2-2: 자르기만 하고 **돌려주지 않고 있었다.** 옆 칸은 지켜지는데
            // 잘린 이름을 읽을 길이 없다 — 눈에 띄는 고장이 아니라 조용히
            // 사라지는 정보라 더 오래 남는다.
            const bool name_clipped = (size.x > metrics.title_w);
            ::editor::clipping::announce_text("EditorInspectorPanel",
                size.x, metrics.title_w, true, true);
            ImGui::PushClipRect(text_pos,
                ImVec2(text_pos.x + metrics.title_w, origin.y + metrics.height), true);
            ImGui::GetWindowDrawList()->AddText(text_pos,
                token_color(ThemeColor::Text, opacity), request.label, end);
            ImGui::PopClipRect();
            if (name_clipped && hovered)
            {
                ImGui::SetTooltip("%.*s", static_cast<int>(end - request.label),
                    request.label);
            }
        }

        // ── 오른쪽 더보기 버튼 ───────────────────────────────────────────
        if (nullptr != request.menu_icon)
        {
            ImGui::SetCursorScreenPos(ImVec2(
                origin.x + inputs.available - metrics.trail - metrics.button,
                center_y - metrics.button * 0.5f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::PushID("menu");
            result.menu_clicked = ImGui::Button(request.menu_icon,
                ImVec2(metrics.button, metrics.button));
            ImGui::PopID();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
        }

        // 머리줄 다음 줄로 커서를 내린다. 위에서 `ItemSize` 로 높이를 이미
        // 알렸으므로 x 만 창 왼쪽으로 되돌리면 된다.
        ImGui::SetCursorScreenPos(ImVec2(window->DC.CursorStartPos.x + window->DC.Indent.x,
            origin.y + metrics.height));

        // 본문은 아이콘 칸만큼 들여쓴다. s&box 도 본문을 머리줄과 같은 x 에
        // 두지 않는다 — 들여쓰기가 "이 줄들은 저 패널의 것" 을 말한다.
        ImGui::Indent(metrics.lead + metrics.expander);

        result.open = open;
        return result;
    }

    void end_inspector_panel()
    {
        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        if (!window->SkipItems)
        {
            const inspector_panel_inputs inputs{
                ImGui::GetContentRegionAvail().x,
                ImGui::GetFrameHeight(),
                ThemePixels(1.f),
                0 };
            const inspector_panel_metrics metrics = measure_inspector_panel(inputs);
            ImGui::Unindent(metrics.lead + metrics.expander);

            // s&box `ComponentSheet` 의 `Layout.Margin = new( 0, 1 )`. 패널
            // 사이가 완전히 붙으면 아래 패널의 경계선이 위 패널 본문의 마지막
            // 줄에 닿는다.
            ImGui::Dummy(ImVec2(0.f, kBorderThickness));
        }
        ImGui::PopID();
    }
}
