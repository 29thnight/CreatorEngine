#include "GameInit.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"

void GameInit::Start()
{
	GameObject* gameManagerObj = GameObject::Find("GameManager");
	if(gameManagerObj)
	{
		m_gameManager = gameManagerObj->GetComponent<GameManager>();
		LoadNextScene();
	}
}

void GameInit::Update(float tick)
{
}

void GameInit::LoadNextScene()
{
	m_gameManager->LoadNextScene();
}
