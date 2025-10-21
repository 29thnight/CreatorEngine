#include "StartButton.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"
void StartButton::Start()
{
	Super::Start();

	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
	}
}

void StartButton::Update(float tick)
{
	Super::Update(tick);
}

void StartButton::ClickFunction()
{
	if(!m_gameManager)
		return;

	GameInstance::GetInstance()->SetAfterLoadSceneIndex((int)SceneType::Stage);
	//GameInstance::GetInstance()->SwitchScene("CharSelectScene");
	m_gameManager->m_nextSceneIndex = (int)SceneType::SelectChar;
	m_gameManager->SwitchNextSceneWithFade();
}