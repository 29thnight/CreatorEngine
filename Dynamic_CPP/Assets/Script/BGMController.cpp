#include "BGMController.h"
#include "pch.h"
#include "SoundName.h"
#include "GameInstance.h"
#include "SoundComponent.h"
void BGMController::Start()
{
	auto instance = GameInstance::GetInstance();
	auto sounds = instance->GetSoundName();
	auto curScene = (SceneType)instance->GetCurrentSceneType();
	std::string curSoundName{};
	m_sound = GetOwner()->GetComponent<SoundComponent>();
	switch (curScene)
	{
	case SceneType::Bootstrap:
		curSoundName = sounds->GetSoudNameRandom("BgmTitle");
		m_pBootstrapObject = GameObject::Find("BootstrapCanvas");
		break;
	case SceneType::SelectChar:
		curSoundName = sounds->GetSoudNameRandom("BgmCharacterSelect");
		break;
	case SceneType::Loading:
		//curSoundName = sounds->GetSoudNameRandom("BgmTitle");
		break;
	case SceneType::Stage:
		curSoundName = sounds->GetSoudNameRandom("BgmStage11");
		break;
	case SceneType::Tutorial:
		curSoundName = sounds->GetSoudNameRandom("BgmStage11");
		break;
	case SceneType::Boss:
		curSoundName = sounds->GetSoudNameRandom("BgmBoss");
		break;
	case SceneType::Credits:
		curSoundName = sounds->GetSoudNameRandom("CreditsBGM");
		break;
	case SceneType::GameOver:
		curSoundName = sounds->GetSoudNameRandom("GameOver");
		if (m_sound)
		{
			m_sound->loop = false;
		}
		break;
	case SceneType::Clear:
		curSoundName = sounds->GetSoudNameRandom("StageClear");
		if (m_sound)
		{
			m_sound->loop = false;
		}
		break;
	default:
		//curSoundName = sounds->GetSoudNameRandom("Test");
		break;
	}



	m_sound = GetOwner()->GetComponent<SoundComponent>();
	if (m_sound)
	{
		m_sound->clipKey = curSoundName;
	}
	if (curScene != SceneType::Bootstrap)
	{
		m_sound->Play();
	}

	//트리거체크 stage에선 초원이랑 사막 사운드 다를수도있음
}

void BGMController::Update(float tick)
{
	if (m_pBootstrapObject && !m_pBootstrapObject->IsEnabled())
	{
		if (m_sound && !m_sound->IsPlaying() && !m_isBootstrapCompleted)
		{
			m_sound->Play();
			m_isBootstrapCompleted = true;
		}
	}
}

