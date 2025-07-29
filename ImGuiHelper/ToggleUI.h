#pragma once
#include <imgui.h>

// ������ �� true ��ȯ, ���´� ������ ���޹޾Ƽ� �ܺο��� �ٲ� �� �ְ� ��
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

        // ����
        ImU32 col_bg;
        if (hovered)
            col_bg = v ? IM_COL32(0, 180, 0, 255) : IM_COL32(180, 0, 0, 255);
        else
            col_bg = v ? IM_COL32(0, 150, 0, 255) : IM_COL32(120, 120, 120, 255);

        // ���
        draw_list->AddRectFilled(p, ImVec2(p.x + width, p.y + height), col_bg, height * 0.5f);

        // �ڵ�
        float circleX = v ? (p.x + width - radius) : (p.x + radius);
        draw_list->AddCircleFilled(ImVec2(circleX, p.y + radius), radius - 1.5f, IM_COL32(255, 255, 255, 255));

        // ���⼭�� �ܼ��� Ŭ�� �̺�Ʈ�� ��ȯ
        return clicked;
    }
}

