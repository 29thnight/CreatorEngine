#include "EditorModeButton.h"
#include "EditorNavContract.h"
#include "EditorClipContract.h"
#include "EditorStateContract.h"

#include "ImGui.h"
#include "EditorTheme.h"

namespace editor::widgets
{
    namespace
    {
        // Idle 은 툴바 바탕(`Chrome`)과 같은 값이다. 그래서 평소에는 버튼 테두리가
        // 보이지 않고 아이콘만 떠 있다 — 계획서가 말한 "flat" 이 그것이다.
        // hover 와 press 에서만 면이 떠오른다.
        ThemeColor mode_surface_token(mode_button_surface surface) noexcept
        {
            switch (surface)
            {
            case mode_button_surface::Hovered: return ThemeColor::PanelRaised;
            case mode_button_surface::Held:    return ThemeColor::Selection;
            case mode_button_surface::Idle:
            case mode_button_surface::Count:
            default:                           return ThemeColor::Chrome;
            }
        }

        mode_button_surface mode_surface_of(bool hovered, bool held) noexcept
        {
            if (held)    { return mode_button_surface::Held; }
            if (hovered) { return mode_button_surface::Hovered; }
            return mode_button_surface::Idle;
        }
    }

    std::uint32_t mode_button_surface_hex(mode_button_surface surface) noexcept
    {
        return ThemeColorHex(mode_surface_token(surface));
    }

    std::uint32_t mode_button_marker_hex() noexcept
    {
        return ThemeColorHex(ThemeColor::Primary);
    }

    std::uint32_t mode_button_text_hex(bool enabled) noexcept
    {
        return ThemeColorHex(enabled ? ThemeColor::Text : ThemeColor::TextDisabled);
    }

    float mode_button_width(const char* icon)
    {
        if (nullptr == icon)
        {
            return 0.f;
        }
        const ImGuiStyle& style = ImGui::GetStyle();
        return ImGui::CalcTextSize(icon, ImGui::FindRenderedTextEnd(icon)).x +
            style.FramePadding.x * 2.f;
    }

    bool draw_mode_button(const mode_button_request& request)
    {
        IM_ASSERT(nullptr != request.icon && "모드 버튼은 아이콘이 ID 의 씨앗이다");
        if (nullptr == request.icon)
        {
            return false;
        }

        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return false;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(mode_button_width(request.icon), ImGui::GetFrameHeight());
        const ImRect bounds(origin, ImVec2(origin.x + size.x, origin.y + size.y));

        const ImGuiID id = ImGui::GetID(request.icon);
        ImGui::ItemSize(bounds, style.FramePadding.y);
        // ★ 꺼진 버튼은 `ImGuiItemFlags_Disabled` 로 **신고한다.** 예전에는
        //   `ButtonBehavior` 만 건너뛰었는데, 그러면 아이템은 평범하게 등록되어
        //   키보드 탐색이 죽은 버튼 위에 **멈추고** Enter 를 눌러도 아무 일이
        //   없다. 마우스만 쓰면 보이지 않는 결함이다.
        if (!ImGui::ItemAdd(bounds, id, nullptr,
                request.enabled ? ImGuiItemFlags_None : ImGuiItemFlags_Disabled))
        {
            return false;
        }
        ::editor::nav::announce_item("EditorModeButton", id, request.enabled);

        // 꺼진 버튼은 상호작용을 아예 묻지 않는다. `BeginDisabled` 로 감싸면
        // 전체 알파가 내려가 marker 까지 흐려지는데, 켜져 있는데 손댈 수 없는
        // 상태(재생 중이 아닐 때의 일시정지)에서 켜짐 표시를 잃을 이유가 없다.
        bool hovered = false;
        bool held = false;
        bool pressed = false;
        if (request.enabled)
        {
            pressed = ImGui::ButtonBehavior(bounds, id, &hovered, &held);
        }

        // W2-3: 상태 행렬.
        //
        // `focus` 와 `nav` 를 같은 자리에서 읽되 **다른 것으로** 적는다.
        // `focus` 는 "이 아이템이 키보드 입력을 받는 자리다"(`NavId` 가 나다)이고,
        // `nav` 는 거기에 "키보드로 와서 커서가 보인다"(`NavCursorVisible`)가
        // 더해진 것이다. 둘을 같은 식으로 적으면 이름만 둘이고 뜻은 하나가 된다.
        //
        // 꺼짐을 스스로 받는 유일한 family 다(`request.enabled`).
        {
            ImGuiContext& state_ctx = *ImGui::GetCurrentContext();
            ::editor::state::declare("EditorModeButton",
                ::editor::state::hover | ::editor::state::active |
                ::editor::state::focus | ::editor::state::nav | ::editor::state::disabled,
                ::editor::state::mixed | ::editor::state::error,
                "mixed 는 Inspector 가 m_selectedEntity 하나만 그려 값이 갈리는 상황이 오지 않고(W2-I3 의 몫), error 는 값 검증이라는 원천이 아직 없다");
            ::editor::state::announce("EditorModeButton",
                (hovered ? ::editor::state::hover : 0u) |
                (held ? ::editor::state::active : 0u) |
                ((state_ctx.NavId == id) ? ::editor::state::focus : 0u) |
                ((state_ctx.NavId == id && state_ctx.NavCursorVisible) ? ::editor::state::nav : 0u) |
            // disabled 는 **둘의 합집합**이다. 위젯이 스스로 받는 꺼짐과,
            // 바깥에서 `ImGui::BeginDisabled` 로 씌운 꺼짐. 앞의 것만 읽으면
            // 인스펙터가 실제로 쓰는 기제(BeginDisabled)를 통째로 못 본다.
                ((request.enabled &&
                  0 == (state_ctx.CurrentItemFlags & ImGuiItemFlags_Disabled))
                     ? 0u : ::editor::state::disabled),
                bounds);
        }

        // 커서를 배경 앞에 그린다 — 표준 `ButtonEx` 와 같은 순서다. 여기는
        // 배경과 커서가 같은 rect 라 3px 확장분이 배경 밖에 남는다.
        ::editor::nav::draw_cursor(bounds, id, "EditorModeButton");

        ImGui::RenderFrame(bounds.Min, bounds.Max,
            ImGui::GetColorU32(ThemeColorValue(mode_surface_token(mode_surface_of(hovered, held)))),
            true, style.FrameRounding);

        const auto packed = [](std::uint32_t rgb)
        {
            return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
        };

        const char* const text_end = ImGui::FindRenderedTextEnd(request.icon);
        const ImVec2 text_size = ImGui::CalcTextSize(request.icon, text_end);
        const ImVec2 text_pos(origin.x + (size.x - text_size.x) * 0.5f,
            origin.y + (size.y - text_size.y) * 0.5f);

        // W2-2: 가운데 정렬이라 넘치면 **양쪽으로** 샌다 — 툴바에서는 옆 버튼
        // 위다. tooltip 은 이 위젯이 이미 갖고 있다(아래 ⑦).
        const bool icon_clipped = (text_size.x > size.x);
        ::editor::clipping::announce_text("EditorModeButton",
            text_size.x, size.x, icon_clipped, nullptr != request.tooltip);
        if (icon_clipped) ImGui::PushClipRect(bounds.Min, bounds.Max, true);
        window->DrawList->AddText(text_pos,
            packed(mode_button_text_hex(request.enabled)), request.icon, text_end);
        if (icon_clipped) ImGui::PopClipRect();

        // 켜짐은 아래 marker 가 든다. 두께는 활성 탭의 것과 같은 토큰을 쓴다 —
        // 같은 뜻("이것이 지금 켜진 것")을 에디터 안에서 두 가지 두께로 그리면
        // 규칙이 보이지 않는다.
        if (request.active)
        {
            const float thickness = ImMax(ThemePixels(EditorThemeTokens::TabActiveMarker), 1.f);
            window->DrawList->AddRectFilled(
                ImVec2(bounds.Min.x, bounds.Max.y - thickness),
                ImVec2(bounds.Max.x, bounds.Max.y),
                packed(mode_button_marker_hex()));
        }

        if (hovered && nullptr != request.tooltip)
        {
            ImGui::SetTooltip("%s", request.tooltip);
        }

        return pressed;
    }
}
