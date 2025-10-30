#include "BGMAmbienceController.h"
#include "pch.h"
#include "SoundName.h"
#include "GameInstance.h"
#include "SoundComponent.h"
void BGMAmbienceController::Start()
{

	auto instance = GameInstance::GetInstance();
	auto sounds = instance->GetSoundName();
	auto curScene = (SceneType)instance->GetCurrentSceneType();
	std::string curSoundName;
	switch (curScene)
	{
	case SceneType::Stage:
		curSoundName = sounds->GetSoudNameRandom("StageBGMAmbienc");
		break;
	case SceneType::Tutorial:
		curSoundName = sounds->GetSoudNameRandom("TutorialBGMAmbienc");
		break;
	case SceneType::Boss:
		curSoundName = sounds->GetSoudNameRandom("BossBGMAmbienc");
		break;
	default:
		curSoundName = sounds->GetSoudNameRandom("TestAmbienc");
		break;
	}

	m_sound = GetOwner()->GetComponent<SoundComponent>();
	m_sound->clipKey = curSoundName;
	m_sound->Play();
}

void BGMAmbienceController::Update(float tick)
{
}

