#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class SwitchingSceneTrigger : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(SwitchingSceneTrigger)
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

private:
	bool m_isFadeInComplete{ false };
	[[Property]]
	float m_fadeDuration{ 1.0f };
	float m_fadeTimer{ 0.0f };
	//[페이드 인 전용]
	//지금은 spriteFont로 해서 TextComponent를 받지만, 아이콘으로 변경될 경우
	//ImageComponent로 변경 필요
	class TextComponent* m_buttonText{ nullptr };
	class TextComponent* m_switchingText{ nullptr };
};
