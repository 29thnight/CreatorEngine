#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class BP003 : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BP003)
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

	Entity* m_ownerEntity = nullptr;

	int m_damage = 0;
	float m_radius = 0.0f;
	//pos를 가지고 있을까? 아님 보스에서 다 정해 줄까? 일단 보스가 전체적으로 다 정해주는걸로 
	//터지는 시간은 장판 패턴에 따라? 아니면 시간을 정해두고? 일단 정할수 있게 해두자
	float m_delay = 0.0f;
	float m_timer = 0.0f;

	bool isInitialize = false;
	bool isAttackStart = false;

	bool ownerDestory = false;

	//그럼 소유하는 엔티티랑 데미지,범위,시간 만 받으면 되려나?
	void Initialize(Entity* owner, int damage, float radius, float delay);

	void Explosion(); //폭발하며 주변 대미지;
};
