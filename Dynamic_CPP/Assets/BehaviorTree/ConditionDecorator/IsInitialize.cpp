#include "IsInitialize.h"
#include "pch.h"
#include "DebugLog.h"
bool IsInitialize::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	// Check if the "Identity" key exists in the blackboard
	bool HasIdentity = blackBoard.HasKey("Identity");
	if (!HasIdentity)
	{
		//LOG("IsInitialize ConditionCheck: No Identity key found in blackboard.");
		//Debug->Log("IsInitialize ConditionCheck: No Identity key found in blackboard.");
		return false; // No Identity key, cannot determine if it's initialized
	}

	// Check if the "Initialized" key exists in the blackboard
	bool HasInitialized = blackBoard.HasKey("Initialized");
	if (!HasInitialized)
	{
		//LOG("IsInitialize ConditionCheck: No Initialized key found in blackboard.");
		return true; // Entity is not initialized -> initialize action will be executed
	}

	bool isInitialized = blackBoard.GetValueAsBool("Initialized");
	if (isInitialized)
	{
		//LOG("IsInitialize ConditionCheck: Entity is initialized.");
		//Debug->Log("IsInitialize ConditionCheck: Entity is initialized.");
		return false; // Entity is initialized -> skip initialize action
	}
	else
	{
		//LOG("IsInitialize ConditionCheck: Entity is not initialized.");
		std::cout << "Entity is not initialized -> initialize action will be executed" << std::endl;
		return true; // Entity is not initialized -> initialize action will be executed
	}
}
