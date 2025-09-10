#include "IsChase.h"
#include "pch.h"

bool IsChase::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool hasClosedTarget = blackBoard.HasKey("ClosedTarget");
	bool useChase = blackBoard.HasKey("ChaseRange");
	bool useChaseOutTime = blackBoard.HasKey("ChaseOutTime");
	bool haState = blackBoard.HasKey("State");

	float chaseRange = 0.0f;
	float chaseOutTime = 0.0f;

	Transform* tr = m_owner->GetComponent<Transform>();
	Mathf::Vector3 pos = tr->GetWorldPosition();


	Mathf::Vector3 dir = Mathf::Vector3::Zero;


	if (!useChase) {
		return false;
	}
	else {
		chaseRange = blackBoard.GetValueAsFloat("ChaseRange");
	}

	if (useChaseOutTime) {
		chaseOutTime = blackBoard.GetValueAsFloat("ChaseOutTime");
	}
	

	if (hasClosedTarget)
	{
		GameObject* closedTarget = blackBoard.GetValueAsGameObject("ClosedTarget");
		if (closedTarget)
		{
		//recalculate distance 
			Transform* closedTransform = closedTarget->GetComponent<Transform>();
			Mathf::Vector3 targetPos = closedTransform->GetWorldPosition();

			dir = targetPos - pos;

			//std::cout << targetPos.x << "," << targetPos.y << "," << targetPos.z << std::endl;
			//std::cout << pos.x << "," << pos.y << "," << pos.z << std::endl;

			//std::cout << "Distance to Target: " << dir.Length() << std::endl;
		}
	}
	else {
		return false;
	}

	/*if (dir!= Mathf::Vector3::Zero)
	{
	}*/
	

	if (dir.Length() < chaseRange) {
		//std::cout << "closed Target in ChaseRange" << std::endl;
		return true;
	}



	if (haState) 
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "Chase")
		{
			if (chaseOutTime > 0) {
				//std::cout << "closed Target Out ranage but chace remain time :" << chaseOutTime << std::endl;
				return true;
			}	
		}
	}


	//std::cout << "closed Target Out ranage" << std::endl;

	return false;
}
