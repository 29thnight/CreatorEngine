#include "EditorAxisField3.h"

#include "ImGui.h"
#include "EditorTheme.h"
#include "EditorPropertyRow.h"

#include <array>
#include "EditorClipContract.h"
#include "EditorStateContract.h"

namespace editor::widgets
{
    namespace
    {
        // Muted axis letters on a shared dark badge; color is not the only cue.
        constexpr std::array<std::uint32_t, static_cast<std::size_t>(axis::Count)> kAxisColors{
            0xD68C83, // X
            0xA6C779, // Y
            0x83A9DC, // Z
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

    std::uint32_t axis_badge_background_hex() noexcept
    {
        return InspectorThemeTokens::AxisBackground;
    }

    const char* axis_badge_label(axis which) noexcept
    {
        return in_range(which) ? kAxisLabels[static_cast<std::size_t>(which)] : nullptr;
    }

    namespace
    {
        ImU32 packed(std::uint32_t rgb) noexcept
        {
            return ImGui::GetColorU32(ImVec4(((rgb >> 16) & 255) / 255.f,
                ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, 1.f));
        }

        // badge 하나. 자리를 잡고 그 위에 칠한다. 오른쪽 모서리를 각지게 두는
        // 것은 바로 뒤에 붙는 입력칸과 한 덩어리로 읽히게 하려는 것이다.
        //
        void draw_badge(axis which, float width, float height)
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(width, height));

            ImDrawList* const list = ImGui::GetWindowDrawList();
            const ImVec2 corner(origin.x + width, origin.y + height);
            list->AddRectFilled(origin, corner, packed(axis_badge_background_hex()),
                ImGui::GetStyle().FrameRounding, ImDrawFlags_RoundCornersLeft);

            const char* const text = axis_badge_label(which);
            const ImVec2 text_size = ImGui::CalcTextSize(text);
            const ImVec2 text_pos(origin.x + (width - text_size.x) * 0.5f,
                origin.y + (height - text_size.y) * 0.5f);

            // W2-2: badge 는 글자 한 자라 실제로 넘칠 일이 거의 없다. 그래도
            // 신고한다 — 이 위젯은 §7.1 의 네 family 중 하나이고, "넘칠 일이
            // 없어서" 계약 밖에 두면 폰트나 배지 폭이 바뀌는 날 아무도 모른다.
            // 가운데 정렬이라 넘치면 **양쪽으로** 샌다.
            // W2-3: badge 는 칠해진 표지다 — 손대는 물건이 아니라 상태가 하나도
            // 없다. **없는 것을 없다고 선언해야** 행렬에 빈칸이 남지 않는다.
            ::editor::state::declare("EditorAxisField3.badge", 0u, ::editor::state::all,
                "축 색 표지는 아이템이 아니다 — 포인터도 키보드도 닿지 않는다");
            ::editor::state::announce("EditorAxisField3.badge", 0u, ImRect(origin, corner));

            const bool badge_clipped = (text_size.x > width);
            ::editor::clipping::announce_text("EditorAxisField3.badge",
                text_size.x, width, badge_clipped, false);
            if (badge_clipped) ImGui::PushClipRect(origin, corner, true);
            list->AddText(text_pos, packed(axis_badge_hex(which)), text);
            if (badge_clipped) ImGui::PopClipRect();
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

        // 세로로 놓으면 축마다 한 줄을 다 쓴다. 가로일 때만 셋으로 나눈다.
        const float slot = request.stacked
            ? total
            : (total - gap * static_cast<float>(kAxisCount - 1)) /
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
            // 가로일 때만 옆으로 붙인다. 세로면 그냥 다음 줄로 떨어진다.
            if (index > 0 && !request.stacked)
            {
                ImGui::SameLine(0.f, gap);
            }

            draw_badge(static_cast<axis>(index), badge_width, height);
            ImGui::SameLine();

            ImGui::SetNextItemWidth(field_width);
            changed |= drag_property_float(kAxisFieldIds[index], request.values + index,
                request.speed, request.min, request.max, request.format, 0, true);
        }

        ImGui::PopStyleVar();

        // 라벨. `DragFloat3` 와 같은 자리에 같은 규약으로 그린다 — `##` 뒤는
        // ID 이므로 표시하지 않고, 숨은 라벨이면 아무것도 그리지 않는다.
        //
        // 세로 배치에서는 라벨을 옆에 붙이지 않는다 — 마지막 축 줄 오른쪽에만
        // 붙어 셋 전체의 이름으로 읽히지 않는다.
        const char* const visible = request.label;
        if (!request.stacked && ('#' != visible[0] || '#' != visible[1]))
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
