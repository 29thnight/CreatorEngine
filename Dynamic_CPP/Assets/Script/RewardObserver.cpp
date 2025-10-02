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
    constexpr float EPS = 1e-3f; // 허용오차

    int reward = GameInstance::GetInstance()->GetRewardAmount();
    rewardText->SetMessage(std::format("{:02}", reward));

    // 효과 재생 중이면 유지
    if (m_activeEffect)
    {
        rewardImage->SetTexture(fullTexture);
        m_elapsed += tick;
        if (m_elapsed >= m_setEffectTime)
        {
            m_activeEffect = false;
            m_elapsed = 0.f;
        }
        return;
    }

    // 0~1로 클램프
    float ratio = std::clamp(gameManager->GetAsisPollutionGaugeRatio(), 0.0f, 1.0f);

    // (옵션) 상승 에지 감지: 이전 프레임 < 임계값 && 이번 프레임 >= 임계값
    bool hitFullNow = (m_prevRatio < 1.0f - EPS) && (ratio >= 1.0f - EPS);
    bool changeReward = m_prevReward < reward;
    m_prevRatio = ratio;
    m_prevReward = reward;

    if (hitFullNow || changeReward) // 가득 찼을 때 한 번만 트리거
    {
        rewardImage->SetTexture(fullTexture);
        rewardImage->clipPercent = 1.f;
        m_activeEffect = true;
        m_elapsed = 0.f;
    }
    else
    {
        rewardImage->SetTexture(fillTexture);
        rewardImage->clipPercent = ratio; // 0~1
    }


}

