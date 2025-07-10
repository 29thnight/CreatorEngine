#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "AsisMove.generated.h"

class AsisMove : public ModuleBehavior
{
public:
   ReflectAsisMove
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(AsisMove)

public:
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override;
	virtual void OnTriggerEnter(const Collision& collider) override;
	virtual void OnTriggerStay(const Collision& collider) override;
	virtual void OnTriggerExit(const Collision& collider) override;
	virtual void OnCollisionEnter(const Collision& collider) override;
	virtual void OnCollisionStay(const Collision& collider) override;
	virtual void OnCollisionExit(const Collision& collider) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override;

private:
	GameObject* m_playerObject{ nullptr };
	Mathf::Vector3 nextMovePoint{ 0.f, 0.f, 0.f };

	Mathf::Vector3 points[3] = {
		{ 0.f, 0.f, 0.f },
		{ 0.f, 0.f, 0.f },
		{ 0.f, 0.f, 0.f }
	};

	[[Property]]
	float moveSpeed = 10.f;
	[[Property]]
	int currentPointIndex = 0;
};
