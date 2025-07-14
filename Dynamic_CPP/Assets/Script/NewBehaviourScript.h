#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

namespace BT
{
	enum class NodeStatus;
}
class BlackBoard;
class NewBehaviourScript : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(NewBehaviourScript)
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

	float m_speed{ 1.0f };

	

	float m_detectRange{ 10.0f };
	float m_chaseRange{ 8.0f };
	float m_attackRange{ 2.0f };
	float m_chaseDelay{ 2.0f }; // Delay to end chase after losing sight of the target
	float m_countDown{ 0.0f }; // All about time countdown
	bool m_isChaseAsis{ false };

	bool m_isDetect{ false };
	bool m_isChasing{ false };
	

	GameObject* player1{ nullptr };
	GameObject* player2{ nullptr };
	GameObject* asis{ nullptr };


	GameObject* target{ nullptr };
	
	void LookAtTarget(Transform* selfTransform, Transform* targetTransform);

	
	bool DetectCondition(const BlackBoard& bb);
	bool ChaseCondition(const BlackBoard& bb);
	bool AttackCondition(const BlackBoard& bb);

	BT::NodeStatus DetectAction(float dt, BlackBoard& bb);
	BT::NodeStatus ChaseAction(float dt, BlackBoard& bb);
	BT::NodeStatus AttackAction(float dt, BlackBoard& bb);
};
