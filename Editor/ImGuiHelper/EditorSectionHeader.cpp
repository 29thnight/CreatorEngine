#include "EditorSectionHeader.h"

#include "ImGui.h"
#include "EditorTheme.h"

namespace editor::widgets
{
    namespace
    {
        // 네 모습이 서로 달라야 쓸모가 있다. 그런데 이 테마는
        // `ImGuiCol_HeaderHovered` 와 `ImGuiCol_HeaderActive` 를 **같은 값**
        // (`Selection`)으로 설정한다(`EditorTheme.cpp`). 그래서 원본처럼
        // Header 계열 세 칸에 기대면 hover 를 살려도 눈에 보이는 차이가 없다 —
        // 닫힌 것이 hover 되면 열린 것과 똑같이 보인다. Header 칸을 버리고
        // 토큰을 직접 읽는다.
        //
        // 넷 다 **불투명**이다. 반투명을 먼저 써 봤다가 버렸다 — 알파는 깔린
        // 색에 따라 결과가 달라지고, 계산해 보니 Selection 45% 가 Panel 위에서
        // 0x41 이라 열림(PanelRaised 0x48)과 6/255 밖에 안 벌어졌다. 같은
        // 머리줄이 Inspector(ChildBg=Panel)와 창 바닥(Canvas)에서 다른 색이 되는
        // 것도 곤란하다. 사다리는 단조롭게 올라간다.
        //
        //   닫힘    Panel        0x343434
        //   열림    PanelRaised  0x484848
        //   hover   Selection    0x525252
        //   누름    Primary      0x2E70EA  (테마가 ButtonActive 에 쓰는 강조색과
        //                                   같다 — 누르는 느낌이 버튼과 일치한다)
        ThemeColor surface_token(section_header_surface surface) noexcept
        {
            switch (surface)
            {
            case section_header_surface::Open:    return ThemeColor::PanelRaised;
            case section_header_surface::Hovered: return ThemeColor::Selection;
            case section_header_surface::Held:    return ThemeColor::Primary;
            case section_header_surface::Closed:
            case section_header_surface::Count:
            default:                              return ThemeColor::Panel;
            }
        }

        section_header_surface surface_of(bool open, bool hovered, bool held) noexcept
        {
            if (held)    { return section_header_surface::Held; }
            if (hovered) { return section_header_surface::Hovered; }
            return open ? section_header_surface::Open : section_header_surface::Closed;
        }
    }

    std::uint32_t section_header_surface_hex(section_header_surface surface) noexcept
    {
        return ThemeColorHex(surface_token(surface));
    }

    namespace
    {
        ImU32 background_color(bool open, bool hovered, bool held) noexcept
        {
            return ImGui::GetColorU32(
                ThemeColorValue(surface_token(surface_of(open, hovered, held))));
        }
    }

    section_header_result draw_section_header(const section_header_request& request)
    {
        section_header_result result{};

        IM_ASSERT(nullptr != request.label && "머리줄은 이름이 ID 의 씨앗이다");
        if (nullptr == request.label)
        {
            return result;
        }

        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return result;
        }

        ImGui::PushID(request.label);
        const ImGuiID id = ImGui::GetID("##Header");

        const ImGuiStyle& style = ImGui::GetStyle();

        // 치수. 원본의 24.0f 자리다. `EditorThemeTokens::ControlHeight` 가 뜻이고
        // `GetFrameHeight()` 가 그 런타임 실현이다 — 폰트 배율이 붙는 쪽을 따라가야
        // 같은 줄의 `ImGui::Button` 과 높이가 어긋나지 않는다. 토큰을 `ThemePixels`
        // 로 직접 재면 style 의 여백은 배율을 안 먹어서 DPI 2 에서 홀로 커진다.
        const float row_height     = ImGui::GetFrameHeight();
        const float spacing        = style.ItemSpacing.x;
        const float checkbox_width = (nullptr != request.enabled) ? row_height : 0.f;
        const float menu_width     = (nullptr != request.menu_icon) ? row_height : 0.f;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 full_size(ImGui::GetContentRegionAvail().x, row_height);
        const ImRect header_rect(origin, origin + full_size);

        // 클릭 영역은 양옆 컨트롤을 **비켜서** 잡는다. 겹치지 않으므로 아래에서
        // 자리를 먼저 잡아도 버튼·체크박스의 클릭을 빼앗지 않는다.
        const float left  = (checkbox_width > 0.f) ? (checkbox_width + spacing) : 0.f;
        const float right = (menu_width > 0.f) ? (menu_width + spacing) : 0.f;
        const ImVec2 zone_min(origin.x + left, origin.y);
        const ImVec2 zone_max(origin.x + full_size.x - right, origin.y + full_size.y);
        const ImRect click_zone(zone_min, zone_max);

        // ① 자리를 먼저 잡고 상태를 읽는다.
        //
        // 원본은 이 블록이 배경 렌더링 **뒤**에 있었다. 그래서 배경을 고를 때
        // `hovered` 는 막 선언된 false 였고 `ImGuiCol_HeaderHovered` 는 한 번도
        // 그려지지 않았다. 그리고 `held` 는 `IsItemActive()` 로 읽었는데, 맨
        // `ItemAdd` 는 ActiveId 를 세우지 않으므로 **언제나 거짓**이었다 —
        // 읽히지 않은 것이 아니라 참이 될 수 없었다. `ButtonBehavior` 가
        // hover·active·press 를 한 번에 맡는 정본이다.
        bool open    = ImGui::GetStateStorage()->GetBool(id, true);
        bool hovered = false;
        bool held    = false;

        // `PressedOnClick` 은 원본 보존이다. 원본은 `IsItemClicked()` 로 토글했고
        // 그것은 **누르는 순간**이다. `ButtonBehavior` 의 기본값은 놓을 때라
        // 플래그를 안 주면 접고 펴는 감각이 바뀐다. ImGui 자신의
        // `CollapsingHeader` 도 누를 때 토글한다.
        ImGui::SetCursorScreenPos(zone_min);
        ImGui::ItemSize(click_zone);
        if (ImGui::ItemAdd(click_zone, id))
        {
            if (ImGui::ButtonBehavior(click_zone, id, &hovered, &held,
                    ImGuiButtonFlags_PressedOnClick))
            {
                open = !open;
                ImGui::GetStateStorage()->SetBool(id, open);
            }
        }

        // ② 배경. 위에서 읽은 상태로 고른다.
        ImGui::RenderFrame(header_rect.Min, header_rect.Max,
            background_color(open, hovered, held), true, style.FrameRounding);

        // ③ 왼쪽 체크박스. 원본 둘째 오버로드의 몫이다.
        if (nullptr != request.enabled)
        {
            const bool before = *request.enabled;
            ImGui::SetCursorScreenPos(origin);
            ImGui::PushID("enabled");
            ImGui::Checkbox("##enabled", request.enabled);
            ImGui::PopID();
            result.enabled_changed = (before != *request.enabled);
        }

        // ④ 오른쪽 버튼.
        if (nullptr != request.menu_icon)
        {
            ImGui::SetCursorScreenPos(
                ImVec2(origin.x + full_size.x - menu_width, origin.y));
            result.menu_clicked =
                ImGui::Button(request.menu_icon, ImVec2(menu_width, 0.f));
        }

        // ⑤ 글자. 클릭 영역 왼쪽 끝에서 프레임 여백만큼 들여 세로 중앙에 놓는다.
        const ImVec2 label_size = ImGui::CalcTextSize(request.label);
        const ImVec2 text_pos(zone_min.x + style.FramePadding.x,
            origin.y + (full_size.y - label_size.y) * 0.5f);
        ImGui::RenderText(text_pos, request.label);

        // ⑥ 다음 줄. 아래 여백은 원본의 3.0f 자리다.
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + full_size.y));
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, EditorThemeTokens::CompactGap));

        result.open = open;
        return result;
    }
}
