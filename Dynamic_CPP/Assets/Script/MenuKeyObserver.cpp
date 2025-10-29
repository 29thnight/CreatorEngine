#include "MenuKeyObserver.h"
#include "InputManager.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "pch.h"

void MenuKeyObserver::Start()
{
	//TODO : 여기서 메뉴 캔버스 활성화가 필요함. 지금은 씬 재시작만 처리하자.(임시)
	//auto gameManagerObj = GameObject::Find("GameManager");
	//if (gameManagerObj)
	//{
	//	m_gameManager = gameManagerObj->GetComponent<GameManager>();
	//}

	m_menuCanvasObject = GameObject::Find("MenuSettingCanvas");
	if (m_menuCanvasObject)
	{
		m_menuCanvasObject->SetEnabled(false);
	}
}

void MenuKeyObserver::Update(float tick)
{
	m_elapsedTime += tick;
	bool isMenuKeyPressed0 = InputManagement->IsControllerButtonReleased(0, ControllerButton::START_BUTTON);
	bool isMenuKeyPressed1 = InputManagement->IsControllerButtonReleased(1, ControllerButton::START_BUTTON);
	if ((isMenuKeyPressed0 || isMenuKeyPressed1))
	{
		if (m_elapsedTime < 0.5f) return; // 디바운스 처리

		GameInstance::GetInstance()->PauseGame();
		m_menuCanvasObject->SetEnabled(true);

		//GameInstance::GetInstance()->m_isIngameMenuOpen = !GameInstance::GetInstance()->m_isIngameMenuOpen;
		//if (!m_menuCanvasObject) return;

		//if(!GameInstance::GetInstance()->m_isIngameMenuOpen)
		//{
		//	GameInstance::GetInstance()->PauseGame();
		//	m_menuCanvasObject->SetEnabled(true);
		//}
		//else
		//{
		//	GameInstance::GetInstance()->ResumeGame();
		//	m_menuCanvasObject->SetEnabled(false);
		//}
		m_elapsedTime = 0.f;
	}
}


