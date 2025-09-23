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
    // UI �ݿ�
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

// �� ĭ �̵� ��Ģ: -1 <-> 0 <-> +1 (�� ����)
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

    // 1) �Ƴ��α� �� �̻� �� ����
    const int axis = SignAxis(dir.x, m_deadZone); // -1 / 0 / +1

    // 2) ���� ����
    const bool risingEdge = (m_axisState == 0 && axis != 0);
    const bool holding = (m_axisState != 0 && axis == m_axisState);
    const bool released = (m_axisState != 0 && axis == 0);

    // 3) ���� ����
    if (risingEdge)
    {
        // ó�� �б� �� �� ĭ �̵�
        SetSlot(Step(m_slot, axis));
        // ����Ʈ �ʱ�ȭ
        m_holdTime = 0.f;
        m_repeatTime = m_repeatDelay;
    }
    else if (holding)
    {
        // �� ������ ���� ����Ʈ
        m_holdTime += m_lastDelta;     // m_lastDelta�� Update���� ������ delta
        m_repeatTime -= m_lastDelta;
        if (m_repeatTime <= 0.f)
        {
            SetSlot(Step(m_slot, axis));      // ���� �������� �� ĭ �߰� �̵�
            m_repeatTime += m_repeatRate;     // ���� �ݺ� ����
        }
    }
    else if (released)
    {
        // �࿡�� �� �� �� Ÿ�̸� ����
        m_holdTime = 0.f;
        m_repeatTime = 0.f;
    }

    // 4) �� ���� ����
    m_axisState = axis;

    // (����) ���� �� ��Ŀ�� ��ġ�� Ŀ��/��Ŀ �̵�
    // UpdateSelectorVisual(m_slot); // ex) x = {-D, 0, +D}
}

