#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class BossHPBar : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BossHPBar)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	std::weak_ptr<GameObject> m_target;
	class Entity* m_entity = nullptr;
	class RectTransformComponent* m_rect = nullptr;
	class ImageComponent* m_image = nullptr;
	class Camera* m_camera = nullptr;

	int m_currentHP{};
	int m_maxHP{};

	float m_shownRatio = 1.0f;     // 화면에 표시 중인 HP 비율
	float m_lerpFrom = 1.0f;
	float m_lerpTo = 1.0f;
	float m_lerpElapsed = 0.0f;
	bool  m_isLerping = false;
	float m_lastHpRatio = 1.0f;      // 직전 프레임의 목표 HP 비율

	static constexpr float kEps = 1e-4f;
	static constexpr float kClipLerpDuration = 0.3f; // 0.3초 동안 감소
};
