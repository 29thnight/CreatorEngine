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

	int reward = GameInstance::GetInstance()->GetRewardAmount();
	std::string rewardStr = std::format("{:02}", reward);
	rewardText->SetMessage(rewardStr);
	rewardImage->clipPercent = reward / 99.f;
}

