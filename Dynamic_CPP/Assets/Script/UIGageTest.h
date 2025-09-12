#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "UIGageTest.generated.h"

class ImageComponent;
class WeaponSlot;
class UIGageTest : public ModuleBehavior
{
public:
   ReflectUIGageTest
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(UIGageTest)
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

	[[Property]]
	float2 centerUV = { 0.5f, 0.5f };
	[[Property]]
	float radiusUV = 1.f;
	[[Property]]
	float percent = 1.f;
	[[Property]]
	float startAngle = 9.8f;
	[[Property]]
	int clockwise = 1;
	[[Property]]
	float featherAngle = 0.5f;
	[[Property]]
	float4 tint = { 1.f, 1.f, 1.f, 1.f };

private:
	ImageComponent* m_imageComponent = nullptr;
	WeaponSlot* m_weaponSlot = nullptr;
};
