#include "SFXPoolManager.h"
#include "pch.h"
#include "GameManager.h"
void SFXPoolManager::Start()
{
	auto childred = GetOwner()->m_childrenIndices;

	for (auto& child : childred)
	{
		auto soundComponent = GameObject::FindIndex(child)->GetComponent<SoundComponent>();
		if (soundComponent)
		{
			SFXSoundPool.push_back(soundComponent);
		}
	}

	auto gameManager = GameObject::Find("GameManager");
	if (gameManager)
	{
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->PushSFXPool(this);
		}
	}

}

void SFXPoolManager::Update(float tick)
{
}

void SFXPoolManager::PlayOneShot(std::string _ClipName)
{
	if (SFXSoundPool.empty())
	{
		return;
	}


	for (auto& channel : SFXSoundPool)
	{
		if (true == channel->IsPlaying()) 
			continue;

		channel->clipKey = _ClipName;
		channel->PlayOneShot();
		break;
	}
}

