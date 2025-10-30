#include "Skip.h"
#include "GameManager.h"
#include "InputManager.h"
#include "pch.h"

void Skip::Start()
{
	GameObject* gmObj = GameObject::Find("GameManager");
	if (gmObj)
	{
		m_gameManager = gmObj->GetComponent<GameManager>();
	}

	if (m_gameManager)
	{
		m_gameManager->m_nextSceneIndex = (int)SceneType::Bootstrap;
		m_gameManager->SetLoadingReq(false);
		m_gameManager->LoadNextScene();
	}
}

void Skip::Update(float tick)
{
	const bool a0 = InputManagement->IsControllerButtonDown(0, ControllerButton::A);
	const bool a1 = InputManagement->IsControllerButtonDown(1, ControllerButton::A);

	const bool just0 = (a0 && !m_prevA0);
	const bool just1 = (a1 && !m_prevA1);

	m_prevA0 = a0;
	m_prevA1 = a1;

	if (just0 || just1)
	{
		if (m_gameManager)
		{
			m_gameManager->SwitchNextSceneWithFade();
		}
	}
}

