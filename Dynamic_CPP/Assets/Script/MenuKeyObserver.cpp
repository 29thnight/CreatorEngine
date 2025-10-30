#include "MenuKeyObserver.h"
#include "InputManager.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "ImageComponent.h"
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

	auto BossObj = GameObject::Find("Boss_edit");
	if (BossObj)
	{
		m_menuImageComponent = GetComponent<ImageComponent>();
		if (m_menuImageComponent)
		{
			m_menuImageComponent->SetTexture(1); //���� �ݴ�Ǵ� �ɸ����� �ؽ�ó�� ����
		}
	}
}

void MenuKeyObserver::Update(float tick)
{
	m_elapsedTime += tick;
	bool isMenuKeyPressed0 = InputManagement->IsControllerButtonReleased(0, ControllerButton::START_BUTTON);
	bool isMenuKeyPressed1 = InputManagement->IsControllerButtonReleased(1, ControllerButton::START_BUTTON);
	if ((isMenuKeyPressed0 || isMenuKeyPressed1))
	{
		if (m_elapsedTime < 0.5f) return; // ��ٿ ó��

		GameInstance::GetInstance()->PauseGame();
		m_menuCanvasObject->SetEnabled(true);
		m_elapsedTime = 0.f;
	}
}


