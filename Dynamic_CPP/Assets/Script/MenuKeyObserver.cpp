#include "MenuKeyObserver.h"
#include "InputManager.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "pch.h"

void MenuKeyObserver::Start()
{
	//TODO : 여기서 메뉴 캔버스 활성화가 필요함. 지금은 씬 재시작만 처리하자.(임시)
	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
	}
}

void MenuKeyObserver::Update(float tick)
{
	const bool isMenuKeyPressed0 = InputManagement->IsControllerButtonPressed(0, ControllerButton::START_BUTTON);
	const bool isMenuKeyPressed1 = InputManagement->IsControllerButtonPressed(1, ControllerButton::START_BUTTON);
	if ((isMenuKeyPressed0 || isMenuKeyPressed1) && !m_isMenuOpened)
	{
		m_isMenuOpened = true;
		if (m_gameManager)
		{
			int currentSceneIndex = GameInstance::GetInstance()->GetCurrentSceneType();
			m_gameManager->m_nextSceneIndex = currentSceneIndex;
			m_gameManager->m_isLoadingReq = true;
			m_gameManager->LoadNextScene(); //현재 씬 재시작
		}
	}

	if (m_isMenuOpened)
	{
		m_gameManager->SwitchNextSceneWithFade();
	}
}


