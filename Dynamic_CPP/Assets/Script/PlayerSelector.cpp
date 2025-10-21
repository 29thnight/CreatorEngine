#include "PlayerSelector.h"
#include <imgui.h>
#include "pch.h"

void PlayerSelector::Start()
{
}

void PlayerSelector::Update(float tick)
{
}

void PlayerSelector::MoveStep(int playerIndex, int step)
{
    if (playerIndex < 0 || playerIndex >= 2) return;
    step = (step < 0) ? -1 : (step > 0) ? +1 : 0;

    const int curIdx = static_cast<int>(m_slot[playerIndex]);
    int       nextIdx = std::clamp(curIdx + step, -1, +1);
    SelectorSlot nextSlot = static_cast<SelectorSlot>(nextIdx);

    // �� �ٸ� �÷��̾ �̹� ������ ��/��� �̵� ����
    if (IsSide(nextSlot) && IsOccupiedByOther(nextSlot, playerIndex)) {
        // ������ �״�� �ΰ�, (���ϸ�) �� ���¸� ����/����
        if (m_axis[playerIndex] != step) {
            m_axis[playerIndex] = step;
            SelectorState s{ playerIndex, m_slot[playerIndex], step };
            //Notify(s); // ����: ���� �˸������ε� ���(ȿ���� ��)
        }
        return;
    }

    // ���� �̵�
    if (nextIdx != curIdx || m_axis[playerIndex] != step) {
        m_slot[playerIndex] = nextSlot;
        m_axis[playerIndex] = step;
        SelectorState s{ playerIndex, m_slot[playerIndex], step };
        Notify(s);
    }
}

void PlayerSelector::SetAxis(int playerIndex, int axis)
{
    if (playerIndex < 0 || playerIndex >= 2) return;
    axis = (axis < 0) ? -1 : (axis > 0) ? +1 : 0;

    // ���� �� �� ĭ �̵�
    if (m_axis[playerIndex] == 0 && axis != 0) {
        MoveStep(playerIndex, axis);
    }
    else if (m_axis[playerIndex] != 0 && axis == 0) {
        // �߸����� ������ - ���� ����(�ุ 0����)
        m_axis[playerIndex] = 0;
        SelectorState s{ playerIndex, m_slot[playerIndex], 0 };
        Notify(s);
    }
    // holding/auto-repeat�� Detector �ʿ��� step���� �� �ִ� �� �����
}

bool PlayerSelector::IsOccupiedByOther(SelectorSlot target, int playerIndex) const {
    if (!IsSide(target)) return false;
    int other = 1 - playerIndex;
    return m_slot[other] == target;
}

void PlayerSelector::RegisterObserver(IPlayerSelectorObserver* observer)
{
    if (!observer) return;
    m_observers.push_back(observer);
    // ��� ���� �ʱ� ���� ����ȭ
    for (int p = 0; p < 2; ++p) 
    {
        SelectorState s{ p, m_slot[p], m_axis[p] };
        observer->OnSelectorChanged(s);
    }
}

void PlayerSelector::UnregisterObserver(IPlayerSelectorObserver* observer)
{
    if (!observer) return;

	std::erase_if(m_observers, [observer](IPlayerSelectorObserver* o) { return o == observer; });
}

SelectorSlot PlayerSelector::GetSlot(int playerIndex) const
{
    if (playerIndex < 0 || playerIndex >= 2) return SelectorSlot::Neutral;
    return m_slot[playerIndex];
}

void PlayerSelector::Notify(const SelectorState& s)
{
    for (auto* o : m_observers)
    {
        o->OnSelectorChanged(s);
    }
}

void PlayerSelector::OnInspectorGUI()
{
    ImGui::Text("Player 0: %s (axis=%d)",
        (m_slot[0] == SelectorSlot::Left) ? "Left" : (m_slot[0] == SelectorSlot::Right) ? "Right" : "Neutral",
        m_axis[0]);
    ImGui::Text("Player 1: %s (axis=%d)",
        (m_slot[1] == SelectorSlot::Left) ? "Left" : (m_slot[1] == SelectorSlot::Right) ? "Right" : "Neutral",
        m_axis[1]);
}

