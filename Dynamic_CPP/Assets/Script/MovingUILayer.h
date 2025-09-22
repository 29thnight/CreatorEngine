#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "MovingUILayer.generated.h"

class MovingUILayer : public ModuleBehavior
{
public:
   ReflectMovingUILayer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(MovingUILayer)
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
	[[Property]]
	float m_movingSpeed{};
	[[Property]]
	float m_waitTick{};
	[[Property]]
	float m_baseY{};
	[[Property]]
	float offset{};
private:
	float m_elapsedTime{};
	bool m_active{};
	Mathf::Vector2 pos{};
	class RectTransformComponent* m_movingTarget{ nullptr };
};
