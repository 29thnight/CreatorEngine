#include "IsChase.h"
#include "pch.h"
#include "DebugLog.h"
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

			//LOG(targetPos.x << "," << targetPos.y << "," << targetPos.z);
			//LOG(pos.x << "," << pos.y << "," << pos.z);

			//LOG("Distance to Target: " << dir.Length());
		}
	}
	else {
		return false;
	}

	/*if (dir!= Mathf::Vector3::Zero)
	{
	}*/
	

	if (dir.Length() < chaseRange) {
		//LOG("closed Target in ChaseRange");
		return true;
	}



	if (haState) 
	{
		std::string state = blackBoard.GetValueAsString("State");
		if (state == "Chase")
		{
			if (chaseOutTime > 0) {
				//LOG("closed Target Out ranage but chace remain time :" << chaseOutTime);
				return true;
			}	
		}
	}


	//LOG("closed Target Out ranage");

	return false;
}
