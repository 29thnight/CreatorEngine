#pragma once
#include <imgui.h>

// 눌렸을 때 true 반환, 상태는 참조로 전달받아서 외부에서 바꿀 수 있게 함
namespace ImGui
{
    inline bool ToggleSwitch(const char* str_id, bool v)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        float height = ImGui::GetFrameHeight();
        float width = height * 1.55f;
        float radius = height * 0.50f;

        ImGui::InvisibleButton(str_id, ImVec2(width, height));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);

        // 색상
        ImU32 col_bg;
        if (hovered)
            col_bg = v ? IM_COL32(0, 180, 0, 255) : IM_COL32(180, 0, 0, 255);
        else
            col_bg = v ? IM_COL32(0, 150, 0, 255) : IM_COL32(120, 120, 120, 255);

        // 배경
        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);

        // 핸들
        float circleX = v ? (p.x + width - radius) : (p.x + radius);
        draw_list->AddCircleFilled(ImVec2(circleX, p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        // 여기서는 단순히 클릭 이벤트만 반환
        return clicked;
    }
}

