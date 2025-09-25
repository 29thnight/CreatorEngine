#include "RewardObserver.h"
#include "pch.h"
#include "TextComponent.h"
#include "ImageComponent.h"
#include "EntityAsis.h"
#include "GameManager.h"

void RewardObserver::Start()
{
	rewardText = GetOwner()->GetComponent<TextComponent>();
	rewardImage = GameObject::Find("GamFill")->GetComponent<ImageComponent>();
	gameManager = GameObject::Find("GameManager")->GetComponent<GameManager>();

}

void RewardObserver::Update(float tick)
{
	if (!rewardText || !rewardImage || !gameManager) return;

	constexpr int fillTexture = 0, fullTexture = 1;

	int reward = GameInstance::GetInstance()->GetRewardAmount();
	std::string rewardStr = std::format("{:02}", reward);
	rewardText->SetMessage(rewardStr);
	if(1.f < gameManager->GetAsisPollutionGaugeRatio())
	{
		rewardImage->SetTexture(fullTexture);
	}
	else
	{
		rewardImage->SetTexture(fillTexture);
		rewardImage->clipPercent = gameManager->GetAsisPollutionGaugeRatio();
	}

}

