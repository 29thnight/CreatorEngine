#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class BP001 : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BP001)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	Entity* m_ownerEntity = nullptr;

	int m_damage = 0; 
	float m_radius = 0.0f; //폭파 반지름
	float m_delay = 0.0f; //중도 폭발 시간
	float m_timer = 0.0f;

	Mathf::Vector3 direction = Mathf::Vector3::Zero; //이동 방향 
	float m_speed = 0.0f;

	bool isInitialize = false;
	bool isAttackStart = false;
	bool isExplode = false;

	bool ownerDestory = false;

	//충돌체 크기도 받아야 하나 싶은대.... 일단 그냥 가자
	void Initialize(Entity* owner, Mathf::Vector3 pos, Mathf::Vector3 dir, int damage, float radius, float delay,float speed);

	void Explosion(); //폭발하며 주변 대미지;
};
