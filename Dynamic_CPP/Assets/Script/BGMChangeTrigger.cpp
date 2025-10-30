#include "BGMChangeTrigger.h"
#include "pch.h"
#include "EntityAsis.h"
#include "SoundComponent.h"
#include "BGMController.h"
#include "SoundName.h"
#include "GameInstance.h"
void BGMChangeTrigger::Start()
{
	BGM = GameObject::Find("BGMSound");

	BGMAmbience = GameObject::Find("BGMAmbience");
}

void BGMChangeTrigger::OnTriggerEnter(const Collision& collision)
{
	if (isSwapped == true) return;
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();

	if (asis)
	{
		auto instance = GameInstance::GetInstance();
		auto sounds = instance->GetSoundName();
		if (BGM && nextBGMTag != "None")
		{
			auto bgm = BGM->GetComponent<SoundComponent>();
			bgm->clipKey = sounds->GetSoudNameRandom(nextBGMTag);
			bgm->Play();


		}

		if (BGMAmbience && nextAmbienceTag != "None")
		{
			auto Ambience = BGMAmbience->GetComponent<SoundComponent>();
			Ambience->clipKey = sounds->GetSoudNameRandom(nextAmbienceTag);
			Ambience->Play();
		}
		isSwapped = true;
	}
}

void BGMChangeTrigger::Update(float tick)
{
}

