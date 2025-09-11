#include "IsGroggy.h"
#include "pch.h"
#include "DebugLog.h"
bool IsGroggy::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool HasIdentity = blackBoard.HasKey("Identity");

	if (!HasIdentity)
	{
		LOG("IsDaed ConditionCheck: No Identity key found in blackboard.");
		return false; // No Identity key, cannot determine if it's a dead entity
	}

	bool HasGroggyTime = blackBoard.HasKey("GroggyTime");

	if (!HasGroggyTime)
	{
		LOG("IsGroggy ConditionCheck: No GroggyTime key found in blackboard.");
		return false; // No GroggyTime key, cannot determine if it's groggy
	}

	float groggyTime = blackBoard.GetValueAsFloat("GroggyTime");

	if (groggyTime > 0.0f)
	{
		LOG("IsGroggy ConditionCheck: Entity is groggy, GroggyTime: " << groggyTime);
		return true; // Entity is groggy
	}
	else
	{
		LOG("IsGroggy ConditionCheck: Entity is not groggy, GroggyTime: " << groggyTime);
		return false; // Entity is not groggy
	}

	
}
