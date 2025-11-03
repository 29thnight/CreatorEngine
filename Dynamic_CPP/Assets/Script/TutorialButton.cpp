#include "TutorialButton.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"

void TutorialButton::Start()
{
	Super::Start();

	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
	}
}

void TutorialButton::Update(float tick)
{
	Super::Update(tick);
}

void TutorialButton::ClickFunction()
{
	if (!m_gameManager)
		return;

	GameInstance::GetInstance()->SetAfterLoadSceneIndex((int)SceneType::Tutorial);
	m_gameManager->m_nextSceneIndex = (int)SceneType::SelectChar;
	m_gameManager->SwitchNextSceneWithFade();
}

