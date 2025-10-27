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
	std::string curSoundName;
	switch (curScene)
	{
	case SceneType::SelectChar:
		curSoundName = sounds->GetSoudNameRandom("SelectCharBGM");
		break;
	case SceneType::Loading:
		curSoundName = sounds->GetSoudNameRandom("LoadingBGM");
		break;
	case SceneType::Stage:
		curSoundName = sounds->GetSoudNameRandom("StageBGM");
		break;
	case SceneType::Tutorial:
		curSoundName = sounds->GetSoudNameRandom("TutorialBGM");
		break;
	case SceneType::Boss:
		curSoundName = sounds->GetSoudNameRandom("BossBGM");
		break;
	case SceneType::Credits:
		curSoundName = sounds->GetSoudNameRandom("CreditsBGM");
		break;
	case SceneType::GameOver:
		curSoundName = sounds->GetSoudNameRandom("GameOverBGM");
		break;
	default:
		curSoundName = sounds->GetSoudNameRandom("Test");
		break;
	}



	m_sound = GetOwner()->GetComponent<SoundComponent>();
	m_sound->clipKey = curSoundName;
	m_sound->Play();

	//트리거체크 stage에선 초원이랑 사막 사운드 다를수도있음
}

void BGMController::Update(float tick)
{

}

