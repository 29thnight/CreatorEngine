#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Entity.h"

class MonsterProjectile : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MonsterProjectile)
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

	Entity* m_owner;

	float m_radius;
	int m_damege;

	bool isInitialize = false;
	bool m_isMoving = false;
	Mathf::Vector3 m_startPos;
	Mathf::Vector3 m_controlPos;
	Mathf::Vector3 m_endPos;
	float m_duration;
	float m_elapsedTime;


	void Initialize(Entity* owner,float radius,int damege,Mathf::Vector3 startPos, Mathf::Vector3 controlPos, Mathf::Vector3 endPos,float calculatedDuration);
	Mathf::Vector3 CalculateBezierPoint(float t, const Mathf::Vector3& p0, const Mathf::Vector3& p1, const Mathf::Vector3& p2);

};
