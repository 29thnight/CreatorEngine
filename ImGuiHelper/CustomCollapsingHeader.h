#pragma once
#include <imgui.h>
#include <imgui_internal.h>

namespace ImGui
{
    inline ImVec2 operator+(const ImVec2& lhs, const ImVec2& rhs)
    {
        return ImVec2(lhs.x + rhs.x, lhs.y + rhs.y);
    }

    inline ImVec2 operator*(const ImVec2& lhs, float scalar)
    {
        return ImVec2(lhs.x * scalar, lhs.y * scalar);
    }

    inline bool DrawCollapsingHeaderWithButton(const char* label, ImGuiTreeNodeFlags flags, const char* buttonLabel, bool* outButtonClicked)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGui::PushID(label);
        ImGuiID id = ImGui::GetID("##Header");

        const float buttonWidth = 24.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImVec2 fullSize = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImRect headerRect(cursorPos, cursorPos + fullSize);

        // 1. ��� ������ (�ð���)
        bool open = ImGui::GetStateStorage()->GetBool(id, true);
        bool hovered = false, held = false;
        ImU32 bgColor = open
            ? ImGui::GetColorU32(ImGuiCol_HeaderActive)
            : (hovered ? ImGui::GetColorU32(ImGuiCol_HeaderHovered) : ImGui::GetColorU32(ImGuiCol_Header));
        ImGui::RenderFrame(headerRect.Min, headerRect.Max, bgColor, true, ImGui::GetStyle().FrameRounding);

        // 2. ������ ��ư ���� ��ġ (�׷��� �Ʒ����� Ŭ�� �浹 �� ��)
        ImVec2 buttonPos = ImVec2(cursorPos.x + fullSize.x - buttonWidth, cursorPos.y);
        ImGui::SetCursorScreenPos(buttonPos);
        bool buttonPressed = ImGui::Button(buttonLabel, ImVec2(buttonWidth, 0));
        if (buttonPressed && outButtonClicked)
            *outButtonClicked = true;

        // 3. ���� ������ Ŭ�� ���� ����
        ImVec2 clickZoneSize = ImVec2(fullSize.x - buttonWidth - spacing, fullSize.y);
        ImRect clickZone(cursorPos, cursorPos + clickZoneSize);
        ImGui::SetCursorScreenPos(cursorPos); // Ŭ�� ���� ��ġ ����

        ImGui::ItemSize(clickZone);
        if (ImGui::ItemAdd(clickZone, id))
        {
            hovered = ImGui::IsItemHovered();
            held = ImGui::IsItemActive();
            if (ImGui::IsItemClicked())
            {
                open = !open;
                ImGui::GetStateStorage()->SetBool(id, open);
            }
        }

        // 4. �ؽ�Ʈ ���� ���
        ImVec2 labelSize = ImGui::CalcTextSize(label);
        ImVec2 textPos = ImVec2(cursorPos.x + ImGui::GetStyle().FramePadding.x,
            cursorPos.y + (clickZoneSize.y - labelSize.y) * 0.5f);
        ImGui::RenderText(textPos, label);

        // 5. ���� �ٷ�
        ImGui::SetCursorScreenPos(ImVec2(cursorPos.x, cursorPos.y + fullSize.y));
        ImGui::PopID();
		ImGui::Dummy(ImVec2(0, 3));

        return open;
    }
}
