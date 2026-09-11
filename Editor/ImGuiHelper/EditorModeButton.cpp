#include "EditorModeButton.h"

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
        if (!ImGui::ItemAdd(bounds, id))
        {
            return false;
        }

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
        window->DrawList->AddText(text_pos,
            packed(mode_button_text_hex(request.enabled)), request.icon, text_end);

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
