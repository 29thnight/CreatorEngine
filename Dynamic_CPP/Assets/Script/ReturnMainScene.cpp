#include "ReturnMainScene.h"
#include "GameManager.h"
#include "ImageComponent.h"
#include "Canvas.h"
#include "pch.h"

void ReturnMainScene::Start()
{
	Super::Start();

	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
	}
}

void ReturnMainScene::Update(float tick)
{
	Super::Update(tick);

	if (!m_gameManager || !m_isClicked)
		return;

	m_gameManager->SwitchNextSceneWithFade();
}

void ReturnMainScene::ClickFunction()
{
	if (!m_gameManager || m_isClicked)
		return;

	m_gameManager->m_nextSceneIndex = (int)SceneType::Bootstrap;
	m_gameManager->LoadNextScene();
	m_imageComponent->SetNavLock(true);
	m_isClicked = true;
}
