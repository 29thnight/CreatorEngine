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
	m_controllerImage = m_pOwner->GetComponent<ImageComponent>();

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
            m_gameManager->SwitchNextSceneWithFade();
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

    if (!m_gameManager || !m_playerSelector || m_isApply) return;

    m_selectHold += m_lastDelta;

	constexpr int ACTIVE = 1; // 선택 완료 텍스처 인덱스
	constexpr int INACTIVE = 0; // 선택 대기 텍스처 인덱스
	constexpr int CON_SELECT_NONE = 0;  // 컨트롤러 이미지 - 선택 안됨
	constexpr int CON_SELECT_WOMAN = 1; // 컨트롤러 이미지 - 여자
	constexpr int CON_SELECT_MAN = 2;   // 컨트롤러 이미지 - 남자
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
                //leftPos->color = { 1, 0, 0, 1 };
                leftPos->SetTexture(ACTIVE);
                m_controllerImage->SetTexture(CON_SELECT_WOMAN);
            }
            else if (charType == CharType::Man)
            {
                //rightPos->color = { 1, 0, 0, 1 };
				rightPos->SetTexture(ACTIVE);
                m_controllerImage->SetTexture(CON_SELECT_MAN);
            }
            m_isApply = true;
        }

    }
    else if (m_isSelectComplete && m_selectHold >= m_requiredSelectHold)
    {
        // GameManager에 해제 요청 (None/None으로 초기화)
        // ClearPlayerInputDevice 를 GameManager에 추가해 사용 (아래 참고)
        m_gameManager->RemovePlayerInputDevice(m_playerIndex, charType, dir);
        if (charType == CharType::Woman)
        {
            //leftPos->color = { 1, 1, 1, 1 };
			leftPos->SetTexture(INACTIVE);
            m_controllerImage->SetTexture(CON_SELECT_NONE);
        }
        else if (charType == CharType::Man)
        {
            //rightPos->color = { 1, 1, 1, 1 };
			rightPos->SetTexture(INACTIVE);
            m_controllerImage->SetTexture(CON_SELECT_NONE);
        }
        m_selectHold = 0.f;
        m_isSelectComplete = false;
        m_isApply = true;
    }
}

void InputDeviceDetector::ReleaseKey()
{
    m_selectHold = 0.f;
	m_isApply = false;
}

void InputDeviceDetector::LeaveSelectScene()
{
    if (!m_isCallStart) return;

    if (!m_selectTimer || m_selectTimer->IsTimerOn() || m_isSelectComplete) return;

    if (!m_isLeaveSelectScene && m_gameManager)
    {
        m_gameManager->m_nextSceneIndex = 0;
        m_gameManager->SetLoadingReq(false);
        m_gameManager->LoadNextScene();
        m_isLeaveSelectScene = true;
    }
}

