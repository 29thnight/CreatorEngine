#include "MenuKeyObserver.h"
#include "InputManager.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "pch.h"

void MenuKeyObserver::Start()
{
	//TODO : ���⼭ �޴� ĵ���� Ȱ��ȭ�� �ʿ���. ������ �� ����۸� ó������.(�ӽ�)
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
			m_gameManager->LoadNextScene(); //���� �� �����
		}
	}

	if (m_isMenuOpened)
	{
		m_gameManager->SwitchNextSceneWithFade();
	}
}


