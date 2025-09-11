#include "ChaseAction.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "Animator.h"
#include "DebugLog.h"
NodeStatus ChaseAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool isSpeed = blackBoard.HasKey("Speed");
	bool HasState = blackBoard.HasKey("State");

	//self
	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	GameObject* closedTarget = blackBoard.GetValueAsGameObject("ClosedTarget");
	CharacterControllerComponent* movement = m_owner->GetComponent<CharacterControllerComponent>();
	Mathf::Vector3 targetpos = Mathf::Vector3::Zero;

	Transform* closedTransform = nullptr;
	if (closedTarget) {
		closedTransform = closedTarget->GetComponent<Transform>();
		if (closedTransform) {
			targetpos = closedTransform->GetWorldPosition();
		}
		else {
			//LOG("ChaseAction: ClosedTarget Transform not found.");
			return NodeStatus::Failure; // Handle case where closedTarget Transform is not found
		}
	}

	/*float speed = 0.0f;
	if (isSpeed)
	{
		speed = blackBoard.GetValueAsFloat("Speed");

	}
	else {
		LOG("not found Speed don`t move";
	}*/
	bool useChase = blackBoard.HasKey("ChaseRange");
	bool useChaseOutTime = blackBoard.HasKey("ChaseOutTime");
	bool useOutDuration = blackBoard.HasKey("ChaseOutDuration");
	float range = blackBoard.GetValueAsFloat("ChaseRange");
	float outTime = 0.0f;
	float duration = 0.0f;
	if (useOutDuration)
	{
		duration = blackBoard.GetValueAsFloat("ChaseOutDuration");
	}
	if (useChaseOutTime)
	{
		outTime = blackBoard.GetValueAsFloat("ChaseOutTime");
	}

	Mathf::Vector3 dir = targetpos - pos;

	if (dir.Length() < range)
	{
		outTime = duration; // Reset outTime if within range
	}
	else {
		outTime -= deltatime; // Decrease outTime if not within range
	}

	blackBoard.SetValueAsFloat("ChaseOutTime", outTime);

	Mathf::Vector2 dir2D = { dir.x, dir.z }; // Assuming y is up, we only care about x and z for 2D direction
	
	dir2D.Normalize();
	
	

	
	
	EntityEnemy* enemy = m_owner->GetComponent<EntityEnemy>();
	if (enemy)
	{
		if (enemy->isKnockBack == false)
		{
			//selfTransform->AddPosition(dir * Speed * deltatime);
			if (movement) {
				movement->Move(dir2D);
				enemy->m_animator->SetParameter("Move", true);
				//LOG("ChaseAction executed. Moving towards target.");
				if (HasState)
				{
					std::string state = blackBoard.GetValueAsString("State");
					if (state == "Chase")
					{
						//LOG("Chase action already in progress.");
						//return NodeStatus::Running; // Continue running if already in chase state
					}
					else
					{
						blackBoard.SetValueAsString("State", "Chase");
						//LOG("Switching to Move state.");
					}
				}
			}
		}
	}
	
	//start chacing is atteck targetting setting
	//setting Target
	if (closedTarget)
	{
		//LOG("ChaseAction: Setting Target to " << closedTarget->ToString());
		blackBoard.SetValueAsGameObject("Target", closedTarget->ToString());
	}

	return NodeStatus::Success;
}
