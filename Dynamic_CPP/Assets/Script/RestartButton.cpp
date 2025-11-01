#include "RestartButton.h"
#include "GameManager.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "pch.h"

void RestartButton::Start()
{
	Super::Start();

	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
	}
}

void RestartButton::Update(float tick)
{
	Super::Update(tick);

	if (!m_gameManager || !m_isClicked)
		return;

	if (m_isEntering)
	{
		int currentScene = GameInstance::GetInstance()->GetCurrentSceneType();
		if (currentScene != (int)SceneType::Boss)
		{
			GameInstance::GetInstance()->ResetAllEnhancements();
		}
	}
	else
	{
		int currentScene = GameInstance::GetInstance()->GetPrevSceneType();
		if (currentScene != (int)SceneType::Boss)
		{
			GameInstance::GetInstance()->ResetAllEnhancements();
		}
	}

	m_gameManager->SwitchPrevSceneWithFade();
}

void RestartButton::ClickFunction()
{
	if (!m_gameManager || m_isClicked)
		return;

	GameInstance::GetInstance()->ResumeGame();
	m_gameManager->m_isLoadingReq = true;
	if (m_isEntering) // 메뉴일때는 켜고, 게임오버 씬일때는 끄기
	{
		m_gameManager->m_prevSceneIndex = GameInstance::GetInstance()->GetCurrentSceneType();
		m_gameManager->LoadPrevSceneManual();
	}
	else
	{
		m_gameManager->LoadPrevScene();
	}
	m_imageComponent->SetNavLock(true);
	m_isClicked = true;
}

