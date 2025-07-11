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

	std::vector<Mathf::Vector3> points;

	int currentPointIndex = 0;

private:
	[[Property]]
	float m_moveSpeed = 2.f;
	[[Property]]
	float m_pathRadius = 1.f;
	[[Property]]
	float m_predictNextTime = 5.0f; // 예측 시간
	[[Property]]
	float m_rotateSpeed = 5.f;

#ifdef _DEBUG
	GameObject* DebugPoint{ nullptr };
#endif // _DEBUG
};
