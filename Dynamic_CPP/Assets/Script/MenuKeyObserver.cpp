#include "MenuKeyObserver.h"
#include "InputManager.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "pch.h"

void MenuKeyObserver::Start()
{
	//TODO : ���⼭ �޴� ĵ���� Ȱ��ȭ�� �ʿ���. ������ �� ����۸� ó������.(�ӽ�)
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
	const bool isMenuKeyPressed0 = InputManagement->IsControllerButtonPressed(0, ControllerButton::START_BUTTON);
	const bool isMenuKeyPressed1 = InputManagement->IsControllerButtonPressed(1, ControllerButton::START_BUTTON);
	if ((isMenuKeyPressed0 || isMenuKeyPressed1))
	{
		m_isMenuOpened = !m_isMenuOpened;
		if (!m_menuCanvasObject) return;

		if(!m_isMenuOpened)
		{
			GameInstance::GetInstance()->PauseGame();
			m_menuCanvasObject->SetEnabled(true);
		}
		else
		{
			GameInstance::GetInstance()->ResumeGame();
			m_menuCanvasObject->SetEnabled(false);
		}
	}
}


