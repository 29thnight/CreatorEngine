#include "WaitAction.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "EntityMonsterA.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"

NodeStatus WaitAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");

	Animator* animator = nullptr;
	std::string identity = "";
	if (!hasIdentity)
	{
		identity = blackBoard.GetValueAsString("Identity");
	}

	if (identity != "MonsterNomal")
	{
		EntityMonsterA* script = m_owner->GetComponent<EntityMonsterA>();
		animator = script->m_animator;
	}
	else {
		EntityEnemy* enemy = m_owner->GetComponent<EntityEnemy>();
		animator = enemy->m_animator;
	}

	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	movement->Move(Mathf::Vector2(0.0f, 0.0f)); // Stop movement during wait

	if (animator)
	{
		animator->SetParameter("Move", false);
		animator->SetParameter("Attack", false);
		animator->SetParameter("Dead", false);
	}
	float waitTime = 0.5f; // Default wait time if not set in blackboard
	static float elapsedTime = 0.0f;
	elapsedTime += deltatime;

	if (elapsedTime >= waitTime)
	{
		elapsedTime = 0.0f; // Reset elapsed time for next tick
		return NodeStatus::Success; // Wait time completed
	}
	else {
		return NodeStatus::Running; // Still waiting
	}
	
}
