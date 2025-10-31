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
	case SceneType::Bootstrap:
		curSoundName = sounds->GetSoudNameRandom("BgmTitle");
		break;
	case SceneType::SelectChar:
		curSoundName = sounds->GetSoudNameRandom("BgmTitle");
		break;
	case SceneType::Loading:
		curSoundName = sounds->GetSoudNameRandom("BgmTitle");
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
		curSoundName = sounds->GetSoudNameRandom("GameOverBGM");
		break;
	default:
		curSoundName = sounds->GetSoudNameRandom("Test");
		break;
	}



	m_sound = GetOwner()->GetComponent<SoundComponent>();
	m_sound->clipKey = curSoundName;
	m_sound->Play();

	//Ʈ����üũ stage���� �ʿ��̶� �縷 ���� �ٸ���������
}

void BGMController::Update(float tick)
{

}

