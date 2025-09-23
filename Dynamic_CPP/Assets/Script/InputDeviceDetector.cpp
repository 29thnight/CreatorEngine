#include "InputDeviceDetector.h"
#include "GameObject.h"
#include "GameManager.h"
#include "pch.h"

void InputDeviceDetector::Start()
{
    m_gameManagerObj = GameObject::Find("GameManager");
    if (m_gameManagerObj)
    {
        m_gameManager = m_gameManagerObj->GetComponent<GameManager>();
		m_playerSelector = m_gameManagerObj->GetComponent<PlayerSelector>();
	}
}

void InputDeviceDetector::Update(float tick)
{
	m_lastDelta = tick;
}

void InputDeviceDetector::SetSlot(Slot slot)
{
    if (m_slot == slot) return;
    m_slot = slot;
    // UI 반영
    // HighlightLeft( slot == Slot::Left );
    // HighlightRight(slot == Slot::Right);
    // HighlightNeutral(slot == Slot::Neutral);
    // PlaySfx("tick");
}

static int SignAxis(float x, float dead) 
{
    if (x > dead) return +1;
    if (x < -dead) return -1;
    return 0;
}

using Slot = InputDeviceDetector::Slot;

// 한 칸 이동 규칙: -1 <-> 0 <-> +1 (랩 없음)
static Slot Step(Slot cur, int dirStep) 
{
    int idx = static_cast<int>(cur) + dirStep;
    if (idx < -1) idx = -1;
    if (idx > 1) idx = 1;
    return static_cast<Slot>(idx);
}

void InputDeviceDetector::MoveSelector(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;

    // 1) 아날로그 → 이산 축 상태
    const int axis = SignAxis(dir.x, m_deadZone); // -1 / 0 / +1

    // 2) 에지 감지
    const bool risingEdge = (m_axisState == 0 && axis != 0);
    const bool holding = (m_axisState != 0 && axis == m_axisState);
    const bool released = (m_axisState != 0 && axis == 0);

    // 3) 상태 전이
    if (risingEdge)
    {
        // 처음 밀기 → 한 칸 이동
        SetSlot(Step(m_slot, axis));
        // 리피트 초기화
        m_holdTime = 0.f;
        m_repeatTime = m_repeatDelay;
    }
    else if (holding)
    {
        // 꾹 누르는 동안 리피트
        m_holdTime += m_lastDelta;     // m_lastDelta는 Update에서 저장한 delta
        m_repeatTime -= m_lastDelta;
        if (m_repeatTime <= 0.f)
        {
            SetSlot(Step(m_slot, axis));      // 같은 방향으로 한 칸 추가 이동
            m_repeatTime += m_repeatRate;     // 다음 반복 간격
        }
    }
    else if (released)
    {
        // 축에서 손 뗌 → 타이머 리셋
        m_holdTime = 0.f;
        m_repeatTime = 0.f;
    }

    // 4) 축 상태 갱신
    m_axisState = axis;

    // (선택) 슬롯 별 포커스 위치로 커서/마커 이동
    // UpdateSelectorVisual(m_slot); // ex) x = {-D, 0, +D}
}

