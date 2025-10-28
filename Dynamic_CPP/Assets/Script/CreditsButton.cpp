#include "CreditsButton.h"
#include "GameInstance.h"
#include "ImageComponent.h"
#include "GameManager.h"
#include "pch.h"

void CreditsButton::Start()
{
	Super::Start();

	auto gameMgrObj = GameObject::Find("GameManager");
	if (gameMgrObj)
	{
		m_gameManager = gameMgrObj->GetComponent<GameManager>();
	}
}

void CreditsButton::Update(float tick)
{
	Super::Update(tick);

	if (m_isClicked && GameInstance::GetInstance()->IsLoadSceneComplete())
	{
		m_gameManager->SwitchNextSceneWithFade();
	}
}

void CreditsButton::ClickFunction()
{
	if (m_isClicked) return;

	if (m_gameManager)
	{
		m_gameManager->m_nextSceneIndex = (int)SceneType::Credits;
		m_gameManager->m_isLoadingReq = false;
		m_gameManager->LoadNextScene();
	}

	m_isClicked = true;
	if (m_imageComponent)
	{
		m_imageComponent->SetNavLock(true);
	}
}

