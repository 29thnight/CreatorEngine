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

    // ★ 다른 플레이어가 이미 점유한 좌/우면 이동 금지
    if (IsSide(nextSlot) && IsOccupiedByOther(nextSlot, playerIndex)) {
        // 슬롯은 그대로 두고, (원하면) 축 상태만 갱신/통지
        if (m_axis[playerIndex] != step) {
            m_axis[playerIndex] = step;
            SelectorState s{ playerIndex, m_slot[playerIndex], step };
            //Notify(s); // 선택: 막힘 알림용으로도 사용(효과음 등)
        }
        return;
    }

    // 정상 이동
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

    // 에지 시 한 칸 이동
    if (m_axis[playerIndex] == 0 && axis != 0) {
        MoveStep(playerIndex, axis);
    }
    else if (m_axis[playerIndex] != 0 && axis == 0) {
        // 중립으로 릴리즈 - 상태 통지(축만 0으로)
        m_axis[playerIndex] = 0;
        SelectorState s{ playerIndex, m_slot[playerIndex], 0 };
        Notify(s);
    }
    // holding/auto-repeat은 Detector 쪽에서 step으로 콜 주는 게 깔끔함
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
    // 등록 직후 초기 상태 동기화
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

