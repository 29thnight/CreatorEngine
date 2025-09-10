#include "IsStartBoss.h"
#include "pch.h"
#include "DebugLog.h"
bool IsStartBoss::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	// Check boos stage Bettle start 
	bool HasIdentity = blackBoard.HasKey("Identity");
	bool HasIsStart = blackBoard.HasKey("IsStart");

	if (!HasIdentity)
	{
		LOG("IsStartBoss: No Identity key found in blackboard.");
		return false; // No Identity key, cannot determine if it's a boss stage
	}
	if (!HasIsStart)
	{
		LOG("IsStartBoss: No IsStart key found in blackboard.");
		return false; // No IsStart key, cannot determine if the boss stage has started
	}


	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity != "Boss1")
	{
		LOG("IsStartBoss: Not a boss stage, Identity: " << Identity);
		return false; // Not a boss stage
	}

	auto IsStart = blackBoard.GetValueAsBool("IsStart");
	if (IsStart)
	{
		//LOG("IsStartBoss: Boss stage has started, Identity: " << Identity);
		return true; // Boss stage has started
	}

	LOG("IsStartBoss: Boss stage has not started yet, Identity: " << Identity);
	return false;
}
