#include "SelectTimer.h"
#include "GameObject.h"
#include "GameManager.h"
#include "TextComponent.h"
#include "pch.h"

void SelectTimer::Start()
{
	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		gameManager = gameManagerObj->GetComponent<GameManager>();
		if(gameManager)
		{
			gameManager->m_nextSceneName = "LoadingScene";
			GameInstance::GetInstance()->SetBeyondSceneName("Fin_Ingame_017");
			gameManager->LoadNextScene();
		}
	}

	timerText = GetComponent<TextComponent>();
}

void SelectTimer::Update(float tick)
{
	if (!gameManager) return;
	int count = gameManager->selectPlayerCount;
	
	if (2 <= count)
	{
		if(0 >= m_remainTimeInternal)
		{
			m_remainTimeInternal = m_remainTimeSetting;
		}

		m_remainTimeInternal -= tick;

		if (0 >= m_remainTimeInternal)
		{
			//·Îµå ¾À ÀüÈ¯
			gameManager->SwitchNextScene();
			return;
		}

		int timer{ static_cast<int>(m_remainTimeInternal) };
		std::string ramineTime = std::format("{:02}", timer);
		if (timerText)
		{
			timerText->SetEnabled(true);
			timerText->SetMessage(ramineTime);
		}
		
	}
	else
	{
		m_remainTimeInternal = -1.f;
		if (timerText)
		{
			timerText->SetMessage("");
			timerText->SetEnabled(false);
		}
	}
}

