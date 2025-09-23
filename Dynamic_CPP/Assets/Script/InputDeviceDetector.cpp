#include "InputDeviceDetector.h"
#include "GameObject.h"
#include "GameManager.h"
#include "PlayerInput.h"
#include "PlayerSelector.h"
#include "pch.h"

static int SignAxis(float x, float dead)
{
    if (x > dead) return +1;
    if (x < -dead) return -1;
    return 0;
}

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

void InputDeviceDetector::MoveSelector(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;

    if (!m_isCallStart || !m_playerSelector) return;

	m_p.axisDiscrete = SignAxis(dir.x, m_deadZone);

    auto stepProcess = [&](int playerIdx, AxisState& st) 
    {
        const int axis = st.axisDiscrete;

        // 에지 감지
        const bool rising = (st.prevAxis == 0 && axis != 0);
        const bool holding = (st.prevAxis != 0 && axis == st.prevAxis);
        const bool released = (st.prevAxis != 0 && axis == 0);

        if (rising) 
        {
            // 처음 밀었으면 1칸 이동
            m_playerSelector->MoveStep(playerIdx, axis);
            st.holdTime = 0.f;
            st.repeatTime = m_repeatDelay;
        }
        else if (holding) 
        {
            st.holdTime += m_lastDelta;
            st.repeatTime -= m_lastDelta;
            if (st.repeatTime <= 0.f) 
            {
                m_playerSelector->MoveStep(playerIdx, axis); // 같은 방향으로 또 한 칸
                st.repeatTime += m_repeatRate;
            }
        }
        else if (released) 
        {
            st.holdTime = 0.f;
            st.repeatTime = 0.f;
            // 축 해제 알림이 필요하면 SetAxis로 0 통지:
            m_playerSelector->SetAxis(playerIdx, 0);
        }

        st.prevAxis = axis;
    };

    stepProcess(m_playerIndex, m_p);
}

