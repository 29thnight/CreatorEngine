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

	m_gameManager->SwitchPrevSceneWithFade();
}

void RestartButton::ClickFunction()
{
	if (!m_gameManager || m_isClicked)
		return;

	m_gameManager->m_isLoadingReq = true;
	m_gameManager->LoadPrevScene();
	m_imageComponent->SetNavLock(true);
	m_isClicked = true;
}

