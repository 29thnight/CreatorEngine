#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Explosion.generated.h"

class EffectComponent;
class Player;
class Explosion : public ModuleBehavior
{
public:
   ReflectExplosion
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Explosion)
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

	void Initialize(Player* _owner);
	Player* m_ownerPlayer = nullptr;
	bool endAttack = false;
	EffectComponent* m_effect;
	
	bool onEffect = false;

	[[Property]]
	float explosionRadius = 3.0f; //범위공격 반경
};
