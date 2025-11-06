#include "SelectTimer.h"
#include "GameObject.h"
#include "GameManager.h"
#include "TextComponent.h"
#include "pch.h"
#include "SFXPoolManager.h"
#include "SoundComponent.h"
#include "ImageComponent.h"
#include "GameInstance.h"
#include "SoundName.h"
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

	//timerText = GetComponent<TextComponent>();
	timerImage = GetComponent<ImageComponent>();
}

void SelectTimer::Update(float tick)
{
	if (!gameManager || !timerImage || m_isSwitchSceneStarted) return;
	int count = gameManager->selectPlayerCount;

	if (m_isTimerOn)
	{
		m_remainTimeInternal -= tick;

		int timer{ static_cast<int>(m_remainTimeInternal) };
		if (0.5f >= m_remainTimeInternal)
		{
			m_isSwitchSceneStarted = true;
			timerImage->SetEnabled(false);
			gameManager->SwitchNextSceneWithFade();
			m_isTimerOn = false;
			return;
		}
		else
		{
			timerImage->SetEnabled(true);
			timerImage->SetTexture(timer);
		}
	}
	
	if (2 <= count)
	{
		if (!m_isTimerOn)
		{
			m_isTimerOn = true;
			gameManager->LoadNextScene();
		}

	}
	else
	{
		m_remainTimeInternal = -1.f;
		if (timerImage)
		{
			timerImage->SetEnabled(false);
			m_isTimerOn = false;
			m_remainTimeInternal = m_remainTimeSetting;
		}
	}
}

