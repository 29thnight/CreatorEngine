#include "WaitAction.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"

NodeStatus WaitAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	EntityEnemy* enemy = m_owner->GetComponent<EntityEnemy>();
	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	movement->Move(Mathf::Vector2(0.0f, 0.0f)); // Stop movement during wait
	Animator* animator = enemy->m_animator;
	if (animator)
	{
		animator->SetParameter("Move", false);
		animator->SetParameter("Attack", false);
		animator->SetParameter("Dead", false);
	}
	float waitTime = 2.0f; // Default wait time if not set in blackboard
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
