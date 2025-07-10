#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <vector>
#include <unordered_map>

namespace ImGui
{
// ���� ���� ������
namespace InternalLayout
{
    struct LayoutState
    {
        enum class Direction { Horizontal, Vertical };
        Direction direction;
        float totalFixedSize = 0.0f;
        float totalWeight = 0.0f;
        std::vector<std::pair<bool, float>> elements; // (isSpring, value)
    };

    static std::vector<LayoutState> layoutStack;
}

// BeginHorizontal: ���� ���� ����
inline void BeginHorizontal(const char* id)
{
    ImGui::PushID(id);
    ImGui::BeginGroup();
    InternalLayout::LayoutState state;
    state.direction = InternalLayout::LayoutState::Direction::Horizontal;
    InternalLayout::layoutStack.push_back(state);
}

// EndHorizontal: ���� ���� ���� �� ��� ��ġ
inline void EndHorizontal()
{
    auto& state = InternalLayout::layoutStack.back();
    float available = ImGui::GetContentRegionAvail().x;
    float spacing = ImGui::GetStyle().ItemSpacing.x;

    float extra = available - state.totalFixedSize - (spacing * (state.elements.size() - 1));
    float offsetX = 0.0f;

    for (size_t i = 0; i < state.elements.size(); ++i)
    {
        if (i > 0)
            ImGui::SameLine();

        auto& [isSpring, value] = state.elements[i];

        float width = isSpring
            ? (state.totalWeight > 0.0f ? extra * (value / state.totalWeight) : 0.0f)
            : value;

        ImGui::Dummy(ImVec2(width, 0.0f));
    }

    ImGui::EndGroup();
    ImGui::PopID();
    InternalLayout::layoutStack.pop_back();
}

// BeginVertical: ���� ���� ����
inline void BeginVertical(const char* id)
{
    ImGui::PushID(id);
    ImGui::BeginGroup();
    InternalLayout::LayoutState state;
    state.direction = InternalLayout::LayoutState::Direction::Vertical;
    InternalLayout::layoutStack.push_back(state);
}

// EndVertical: ���� ���� ���� �� ��� ��ġ
inline void EndVertical()
{
    auto& state = InternalLayout::layoutStack.back();
    float available = ImGui::GetContentRegionAvail().y;
    float spacing = ImGui::GetStyle().ItemSpacing.y;

    float extra = available - state.totalFixedSize - (spacing * (state.elements.size() - 1));
    float offsetY = 0.0f;

    for (size_t i = 0; i < state.elements.size(); ++i)
    {
        auto& [isSpring, value] = state.elements[i];
        float height = isSpring
            ? (state.totalWeight > 0.0f ? extra * (value / state.totalWeight) : 0.0f)
            : value;

        ImGui::Dummy(ImVec2(0.0f, height));
    }

    ImGui::EndGroup();
    ImGui::PopID();
    InternalLayout::layoutStack.pop_back();
}

// Spring: ���� ���� �� �Ϻθ� �����ϴ� ��¥ ��� ����
inline void Spring(float weight = 1.0f, float spacing = -1.0f)
{
    if (InternalLayout::layoutStack.empty())
        return;

    auto& state = InternalLayout::layoutStack.back();
    state.totalWeight += weight;
    state.elements.emplace_back(true, weight);
}

// Fixed: ������ ũ���� ��� ���� (���̾ƿ� ũ�� ������)
inline void Fixed(float size)
{
    if (InternalLayout::layoutStack.empty())
        return;

    auto& state = InternalLayout::layoutStack.back();
    state.totalFixedSize += size;
    state.elements.emplace_back(false, size);
}
}