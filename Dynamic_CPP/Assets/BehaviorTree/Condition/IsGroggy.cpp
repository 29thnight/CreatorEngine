#include "IsGroggy.h"
#include "pch.h"

bool IsGroggy::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool HasIdentity = blackBoard.HasKey("Identity");

	if (!HasIdentity)
	{
		std::cout << "IsDaed ConditionCheck: No Identity key found in blackboard." << std::endl;
		return false; // No Identity key, cannot determine if it's a dead entity
	}

	bool HasGroggyTime = blackBoard.HasKey("GroggyTime");

	if (!HasGroggyTime)
	{
		std::cout << "IsGroggy ConditionCheck: No GroggyTime key found in blackboard." << std::endl;
		return false; // No GroggyTime key, cannot determine if it's groggy
	}

	float groggyTime = blackBoard.GetValueAsFloat("GroggyTime");

	if (groggyTime > 0.0f)
	{
		std::cout << "IsGroggy ConditionCheck: Entity is groggy, GroggyTime: " << groggyTime << std::endl;
		return true; // Entity is groggy
	}
	else
	{
		std::cout << "IsGroggy ConditionCheck: Entity is not groggy, GroggyTime: " << groggyTime << std::endl;
		return false; // Entity is not groggy
	}

	
}
