#include "RewardObserver.h"
#include "pch.h"
#include "TextComponent.h"
#include "ImageComponent.h"
#include "GameManager.h"

void RewardObserver::Start()
{
	rewardText = GetOwner()->GetComponent<TextComponent>();
	rewardImage = GameObject::Find("GamFill")->GetComponent<ImageComponent>();
}

void RewardObserver::Update(float tick)
{
	if (!rewardText || !rewardImage) return;

	std::string rewardStr = std::format("{:02}", GameManager::GetReward());
	rewardText->SetMessage(rewardStr);
	rewardImage->clipPercent = GameManager::GetReward() / 99.f;
}

