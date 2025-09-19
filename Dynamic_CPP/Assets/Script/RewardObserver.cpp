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

	//TODO :	바로 리워드를 받는게 아니라, 오염도 게이지가 차는 걸로 변경하고
	//			오염도 게이지가 가득 찼을 때 리워드를 받도록 변경 필요.
	int reward = GameInstance::GetInstance()->GetRewardAmount();
	std::string rewardStr = std::format("{:02}", reward);
	rewardText->SetMessage(rewardStr);
	rewardImage->clipPercent = reward / 99.f;
}

