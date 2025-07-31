#include "IsStartBoss.h"
#include "pch.h"

bool IsStartBoss::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	// Check boos stage Bettle start 
	bool HasIdentity = blackBoard.HasKey("Identity");
	bool HasIsStart = blackBoard.HasKey("IsStart");

	if (!HasIdentity)
	{
		std::cout << "IsStartBoss: No Identity key found in blackboard." << std::endl;
		return false; // No Identity key, cannot determine if it's a boss stage
	}
	if (!HasIsStart)
	{
		std::cout << "IsStartBoss: No IsStart key found in blackboard." << std::endl;
		return false; // No IsStart key, cannot determine if the boss stage has started
	}


	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity != "Boss1")
	{
		std::cout << "IsStartBoss: Not a boss stage, Identity: " << Identity << std::endl;
		return false; // Not a boss stage
	}

	auto IsStart = blackBoard.GetValueAsBool("IsStart");
	if (IsStart)
	{
		//std::cout << "IsStartBoss: Boss stage has started, Identity: " << Identity << std::endl;
		return true; // Boss stage has started
	}

	std::cout << "IsStartBoss: Boss stage has not started yet, Identity: " << Identity << std::endl;
	return false;
}
