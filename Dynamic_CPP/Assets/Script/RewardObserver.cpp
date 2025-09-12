#include "RewardObserver.h"
#include "pch.h"
#include "TextComponent.h"
#include "GameManager.h"

void RewardObserver::Start()
{
	rewardText = GetOwner()->GetComponent<TextComponent>();
}

void RewardObserver::Update(float tick)
{
	if (!rewardText) return;

	std::string rewardStr = std::format("{:02}", GameManager::GetReward());
	rewardText->SetMessage(rewardStr);
}

