#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "CriticalMark.h"
class BehaviorTreeComponent;
class BlackBoard;
class HitResult;
class Animator;
class EffectComponent;
class EntityEnemy : public Entity
{
public:
	MODULE_BEHAVIOR_BODY(EntityEnemy)
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

	CriticalMark criticalMark;
	virtual void SendDamage(Entity* sender, int damage) override;
	virtual void SendKnockBack(Entity* sender, Mathf::Vector2 KnockBackForce);
	bool isDead;
	BehaviorTreeComponent* enemyBT;
	BlackBoard* blackBoard;
	bool isDamage = false;

	int attackCount = 0;
	Animator* m_animator;
	GameObject* enemy;

	EffectComponent* markEffect = nullptr; //크리티컬 마크 
	void MeleeAttack();
};
