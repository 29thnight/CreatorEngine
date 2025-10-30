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

	float m_shownRatio = 1.0f;     // ȭ�鿡 ǥ�� ���� HP ����
	float m_lerpFrom = 1.0f;
	float m_lerpTo = 1.0f;
	float m_lerpElapsed = 0.0f;
	bool  m_isLerping = false;
	float m_lastHpRatio = 1.0f;      // ���� �������� ��ǥ HP ����

	static constexpr float kEps = 1e-4f;
	static constexpr float kClipLerpDuration = 0.3f; // 0.3�� ���� ����
};
