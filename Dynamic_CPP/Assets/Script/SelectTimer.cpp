#include "SelectTimer.h"
#include "GameObject.h"
#include "GameManager.h"
#include "TextComponent.h"
#include "pch.h"

inline constexpr auto IsStageOrTutorial = [](SceneType t) noexcept -> bool
{
	return t == SceneType::Stage || t == SceneType::Tutorial;
};

void SelectTimer::Start()
{
	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		gameManager = gameManagerObj->GetComponent<GameManager>();

		if(gameManager)
		{
			int loadSceneType = GameInstance::GetInstance()->GetAfterLoadSceneIndex();
			if (IsStageOrTutorial(static_cast<SceneType>(loadSceneType)))
			{
				gameManager->m_nextSceneIndex = loadSceneType;
				gameManager->SetLoadingReq();
			}
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
		if (!m_isTimerOn)
		{
			m_isTimerOn = true;
			gameManager->LoadNextScene();
		}

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
			m_isTimerOn = false;
		}
	}
}

