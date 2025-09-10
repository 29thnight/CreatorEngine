#include "IsAtteck.h"
#include "pch.h"
#include "DebugLog.h"
bool IsAtteck::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	//LOG("IsAtteck ConditionCheck: Checking if entity is in attack range.");

	//this based target logic
	bool isTarget = blackBoard.HasKey("Target");
	bool useAttack = blackBoard.HasKey("AtkRange");
	bool hasState = blackBoard.HasKey("State");
	bool hasAtkDelay = blackBoard.HasKey("AtkDelay");


	//if (hasState) {
	//	std::string state = blackBoard.GetValueAsString("State");
	//	if (state == "Atteck") {
	//		if (hasAtkDelay) {
	//			float AtkDelay = blackBoard.GetValueAsFloat("AtkDelay");

	//			if (AtkDelay > 0.0f) {
	//				LOG("IsAtteck ConditionCheck: Attack duration has ended.");
	//				return true; // Attack duration has ended, can attack
	//			}
	//		}
	//	}
	//}



	if (!useAttack)
	{
		//LOG("Not found AtkRange None used Attack Entity : " + m_owner->GetHashedName().ToString());
		return false; // No attack range defined, cannot attack
	}

	if (!isTarget)
	{
		//LOG("IsAtteck ConditionCheck: No target found.");
		return false; // No target to attack
	}

	Transform* selfTransform = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	GameObject* target = blackBoard.GetValueAsGameObject("Target");
	Transform* targetTransform = target->GetComponent<Transform>();
	Mathf::Vector3 targetPos = targetTransform->GetWorldPosition();
	Mathf::Vector3 dir = targetPos - pos;
	float atkRange = blackBoard.GetValueAsFloat("AtkRange");

	if (dir.Length() < atkRange)
	{
		//LOG("IsAtteck ConditionCheck: Entity is within attack range.");
		return true; // Entity is within attack range, condition met
	}


	//this distance based logic 
	//bool isP1 = blackBoard.HasKey("Player1");
	//bool isP2 = blackBoard.HasKey("Player2");

	//if (!isP1 && !isP2) {
	//	LOG("IsAtteck ConditionCheck: No player found.");
	//	return false; // No player to attack
	//}

	//Transform* selfTransform = m_owner->GetComponent<Transform>();
	//Mathf::Vector3 pos = selfTransform->GetWorldPosition();

	//GameObject* Player1 = nullptr;
	//Transform* player1Transform = nullptr;
	//Mathf::Vector3 p1dir;

	//if (isP1) {
	//	Player1 = blackBoard.GetValueAsGameObject("Player1");
	//	player1Transform = Player1->GetComponent<Transform>();
	//	Mathf::Vector3 p1pos = player1Transform->GetWorldPosition();
	//	p1dir = p1pos - pos;
	//}

	//GameObject* Player2 = nullptr;
	//Transform* player2Transform = nullptr;
	//Mathf::Vector3 p2dir;

	//if (isP2) {
	//	Player2 = blackBoard.GetValueAsGameObject("Player2");
	//	player2Transform = Player2->GetComponent<Transform>();
	//	Mathf::Vector3 p2pos = player2Transform->GetWorldPosition();
	//	p2dir = p2pos - pos;
	//}
	//
	//float atkRange = blackBoard.GetValueAsFloat("eNorAtkRange");
	//
	//if (isP1&&isP2)
	//{
	//	if (p1dir.Length() < atkRange || p2dir.Length() < atkRange) 
	//	{
	//		//condition is none selected target, target selected action will be executed
	//		return true;
	//	}
	//}
	//else if (isP1) 
	//{
	//	if (p1dir.Length() < atkRange) 
	//	{
	//		return true;
	//	}
	//}
	//else if (isP2) 
	//{
	//	if (p2dir.Length() < atkRange) 
	//	{
	//		return true;
	//	}
	//}
	

	
	//Asis a non-Attackable entity, other logic can be added here if needed
	/*Transform asisTransform = blackBoard.GetValueAsTransform("Asis");
	Mathf::Vector3 asispos = asisTransform.GetWorldPosition();

	Mathf::Vector3 dir = asispos - pos;*/
		
	//LOG("IsAtteck ConditionCheck: Entity is out of attack range.");
	return false;
}
