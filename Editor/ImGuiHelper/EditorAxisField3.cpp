#include "EditorAxisField3.h"

#include "ImGui.h"
#include "EditorTheme.h"

#include <array>

namespace editor::widgets
{
    namespace
    {
        // 축 색. 팔레트의 의미 색(Error 0xFB5A5A · Positive 0x5AEB5C ·
        // Primary 0x2E70EA)보다 어둡고 채도가 낮다. 두 가지를 동시에 만족해야
        // 했다 — 의미 색과 눈에 띄게 다를 것, 그리고 흰 글자가 그 위에서 읽힐 것.
        //
        // 흰 글자 대비(WCAG 상대 휘도 기준)는 X 5.4:1 · Y 5.1:1 · Z 5.3:1 이다.
        // 밝은 초록(Positive)을 그대로 쓰면 2.1:1 이라 badge 의 글자가 사라진다.
        // 그 계산은 주석이 아니라 단정이다 — `EditorThemeSelfTest` 가 여기서
        // 내보내는 두 값으로 대비를 다시 재고 3:1 미만이면 붉어진다.
        constexpr std::array<std::uint32_t, static_cast<std::size_t>(axis::Count)> kAxisColors{
            0xC0392B, // X
            0x4F7A28, // Y
            0x2D6FA8, // Z
        };

        constexpr std::array<const char*, static_cast<std::size_t>(axis::Count)> kAxisLabels{
            "X", "Y", "Z"
        };

        // 필드의 ImGui ID. 축마다 고정이라 열 순서가 바뀌어도 편집 상태가 딴
        // 칸으로 옮겨 가지 않는다.
        constexpr std::array<const char*, static_cast<std::size_t>(axis::Count)> kAxisFieldIds{
            "##ax", "##ay", "##az"
        };

        constexpr std::size_t kAxisCount = static_cast<std::size_t>(axis::Count);

        bool in_range(axis which) noexcept
        {
            const auto index = static_cast<std::size_t>(which);
            return index < kAxisCount;
        }
    }

    std::uint32_t axis_badge_hex(axis which) noexcept
    {
        return in_range(which) ? kAxisColors[static_cast<std::size_t>(which)] : 0u;
    }

    std::uint32_t axis_badge_text_hex() noexcept
    {
        return ThemeColorHex(ThemeColor::Text);
    }

    const char* axis_badge_label(axis which) noexcept
    {
        return in_range(which) ? kAxisLabels[static_cast<std::size_t>(which)] : nullptr;
    }

    namespace
    {
        ImU32 packed(std::uint32_t rgb) noexcept
        {
            return IM_COL32((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff, 255);
        }

        // badge 하나. 자리를 잡고 그 위에 칠한다. 오른쪽 모서리를 각지게 두는
        // 것은 바로 뒤에 붙는 입력칸과 한 덩어리로 읽히게 하려는 것이다.
        //
        // 다만 지금 테마에서는 그 덩어리의 절반이 보이지 않는다. 실제 화면을
        // 찍어 보니 손대기 전의 입력칸에 경계가 없고 badge 만 떠 있다 —
        // `EditorTheme.cpp` 가 `ImGuiCol_FrameBg` 를 `Canvas` 로 두는데
        // 인스펙터 바탕도 `Canvas` 라 두 색이 같기 때문이다. hover 하면
        // `FrameBgHovered`(`PanelRaised`)가 떠올라 칸이 드러난다. 이 위젯이
        // 만든 성질이 아니라 테마가 두 칸을 같은 값으로 둔 결과이고, 같은
        // 충돌이 `EditorSectionHeader` 의 hover 를 죽였던 것과 같은 양식이다.
        // W1 이 아직 `progress` 인 자리라 여기서 값을 바꾸지 않고 적어 둔다.
        void draw_badge(axis which, float width, float height)
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, height));

            ImDrawList* const list = ImGui::GetWindowDrawList();
            const ImVec2 corner(origin.x + width, origin.y + height);
            list->AddRectFilled(origin, corner, packed(axis_badge_hex(which)),
                ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

            const char* const text = axis_badge_label(which);
            const ImVec2 text_size = ImGui::CalcTextSize(text);
            const ImVec2 text_pos(origin.x + (width - text_size.x) * 0.5f,
                origin.y + (height - text_size.y) * 0.5f);
            list->AddText(text_pos, packed(axis_badge_text_hex()), text);
        }
    }

    bool draw_axis_field3(const axis_field3_request& request)
    {
        IM_ASSERT(nullptr != request.label && "축 줄은 이름이 ID 의 씨앗이다");
        IM_ASSERT(nullptr != request.values && "값 없는 줄은 그릴 것이 없다");

        if (nullptr == request.label || nullptr == request.values)
        {
            return false;
        }

        ImGuiWindow* const window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return false;
        }

        const ImGuiStyle& style = ImGui::GetStyle();
        const float height = ImGui::GetFrameHeight();

        // badge 폭은 글자를 담을 만큼이되 입력칸을 먹지 않을 만큼이다.
        // `GetFrameHeight()` 를 따라가는 이유는 `EditorSectionHeader` 와 같다 —
        // ImGui 1.92 의 폰트 배율은 `ImGuiStyle` 의 여백을 키우지 않으므로
        // 토큰을 직접 배율하면 DPI 2 에서 이 칸만 홀로 커진다.
        const float badge_width = height * 0.62f;

        // 셋이 가용폭을 나눠 가진다. `CalcItemWidth` 는 `SetNextItemWidth` 를
        // 존중하므로 호출자가 폭을 정해 둔 자리에서도 어긋나지 않는다.
        const float total = ImGui::CalcItemWidth();
        const float gap = style.ItemInnerSpacing.x;
        const float slot = (total - gap * static_cast<float>(kAxisCount - 1)) /
            static_cast<float>(kAxisCount);
        const float field_width = ImMax(slot - badge_width, 1.f);

        bool changed = false;

        ImGui::PushID(request.label);
        ImGui::BeginGroup();

        // badge 와 입력칸을 붙인다. 사이에 기본 간격이 들어가면 한 덩어리로
        // 읽히지 않는다.
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, style.ItemSpacing.y));

        for (std::size_t index = 0; index < kAxisCount; ++index)
        {
            if (index > 0)
            {
                ImGui::SameLine(0.f, gap);
            }

            draw_badge(static_cast<axis>(index), badge_width, height);
            ImGui::SameLine();

            ImGui::SetNextItemWidth(field_width);
            changed |= ImGui::DragFloat(kAxisFieldIds[index], request.values + index,
                request.speed, request.min, request.max, request.format);
        }

        ImGui::PopStyleVar();

        // 라벨. `DragFloat3` 와 같은 자리에 같은 규약으로 그린다 — `##` 뒤는
        // ID 이므로 표시하지 않고, 숨은 라벨이면 아무것도 그리지 않는다.
        const char* const visible = request.label;
        if ('#' != visible[0] || '#' != visible[1])
        {
            ImGui::SameLine(0.f, style.ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(visible, ImGui::FindRenderedTextEnd(visible));
        }

        ImGui::EndGroup();
        ImGui::PopID();

        return changed;
    }
}
