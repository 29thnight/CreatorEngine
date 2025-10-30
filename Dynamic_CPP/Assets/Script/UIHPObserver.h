#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class UIHPObserver : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(UIHPObserver)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

public:
	class Entity* m_entity = nullptr;

private:
	class ImageComponent* m_image = nullptr;

	int m_currentHP{};
	int m_maxHP{};
	float m_warningPersent{ 0.2f };

	Mathf::Color4 m_warningColor{ 0.992f, 0.541f, 0.541f, 1.0f };

private:
	float m_blinkTimer = 0.0f; // 누적 시간
	float m_blinkPeriod = 0.5f; // 한 주기(초) - 빠르기 조절용
	float m_onRatio = 0.5f; // 주기 중 경고색 유지 비율 (0~1)
};
