#include "InputDeviceDetector.h"
#include "GameObject.h"
#include "GameManager.h"
#include "PlayerInput.h"
#include "PlayerSelector.h"
#include "ImageComponent.h"
#include "SelectTimer.h"
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

    GameObject* lefttargetObj = nullptr;
    GameObject* righttargetObj = nullptr;
    ImageComponent* LPosTex = nullptr;
    ImageComponent* RPosTex = nullptr;
    if (lefttargetObj = GameObject::Find("readyButtonSlot (0)"))
    {
        LPosTex = lefttargetObj->GetComponent<ImageComponent>();
        if (LPosTex) leftPos = LPosTex;
    }

    if (righttargetObj = GameObject::Find("readyButtonSlot (1)"))
    {
        RPosTex = righttargetObj->GetComponent<ImageComponent>();
        if (RPosTex) rightPos = RPosTex;
    }

	GameObject* timerObj = nullptr;
    if (timerObj = GameObject::Find("CharSelectTimer"))
    {
        m_selectTimer = timerObj->GetComponent<SelectTimer>();
	}
}

void InputDeviceDetector::Update(float tick)
{
	m_lastDelta = tick;
	if (m_isLeaveSelectScene)
    {
		int& nextSceneIndex = m_gameManager->m_nextSceneIndex;
        if(GameInstance::GetInstance()->IsLoadSceneComplete() && nextSceneIndex == (int)SceneType::Bootstrap)
        {
			m_isLeaveSelectScene = false;
            m_gameManager->SwitchNextScene();
        }
    }
}

void InputDeviceDetector::MoveSelector(Mathf::Vector2 dir)
{
	if (!m_isCallStart) return;

    if (!m_playerSelector || m_isSelectComplete) return;

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

void InputDeviceDetector::CharSelect()
{
    if (!m_isCallStart) return;

    if (!m_gameManager || !m_playerSelector) return;

    m_selectHold += m_lastDelta;

    // 아직 선택 전이고, 일정 시간 이상 홀드되면 "선택 확정"
    if (!m_isSelectComplete && m_selectHold >= m_requiredSelectHold)
    {
        // 현재 플레이어의 슬롯 조회
        SelectorSlot slot = m_playerSelector->GetSlot(m_playerIndex); // 좌/중립/우
        // 슬롯 -> 캐릭터 타입 매핑
        charType = CharType::None;
        switch (slot)
        {
        case SelectorSlot::Left:   charType = CharType::Woman; break; // 좌측=여자
        case SelectorSlot::Right:  charType = CharType::Man;   break; // 우측=남자
        default:                   charType = CharType::None;  break;
        }

        // 플레이어 방향(P1=Left, P2=Right) 고정
        dir = (m_playerIndex == 0) ? PlayerDir::Left : PlayerDir::Right;

        if (charType != CharType::None)
        {
            m_gameManager->SetPlayerInputDevice(m_playerIndex, charType, dir); // 연결됨. :contentReference[oaicite:0]{index=0}
            m_isSelectComplete = true;
            m_selectHold = 0.f;
            if (charType == CharType::Woman)
            {
                leftPos->color = { 1, 0, 0, 1 };
            }
            else if (charType == CharType::Man)
            {
                rightPos->color = { 1, 0, 0, 1 };
            }
        }
    }
    else if (m_isSelectComplete && m_selectHold >= m_requiredSelectHold)
    {
        // GameManager에 해제 요청 (None/None으로 초기화)
        // ClearPlayerInputDevice 를 GameManager에 추가해 사용 (아래 참고)
        m_gameManager->RemovePlayerInputDevice(m_playerIndex, charType, dir);
        if (charType == CharType::Woman)
        {
            leftPos->color = { 1, 1, 1, 1 };
        }
        else if (charType == CharType::Man)
        {
            rightPos->color = { 1, 1, 1, 1 };
        }
        m_selectHold = 0.f;
        m_isSelectComplete = false;
    }
}

void InputDeviceDetector::ReleaseKey()
{
    m_selectHold = 0.f;
}

void InputDeviceDetector::LeaveSelectScene()
{
    if (!m_isCallStart) return;

    if (!m_selectTimer || m_selectTimer->IsTimerOn()) return;

    if (!m_isLeaveSelectScene && m_gameManager)
    {
        m_gameManager->m_nextSceneIndex = 0;
        m_gameManager->SetLoadingReq(false);
        m_gameManager->LoadNextScene();
        m_isLeaveSelectScene = true;
    }
}

