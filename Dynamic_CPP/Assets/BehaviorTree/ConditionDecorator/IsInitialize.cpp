#include "IsInitialize.h"
#include "pch.h"

bool IsInitialize::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	// Check if the "Identity" key exists in the blackboard
	bool HasIdentity = blackBoard.HasKey("Identity");
	if (!HasIdentity)
	{
		std::cout << "IsInitialize ConditionCheck: No Identity key found in blackboard." << std::endl;
		//Debug->Log("IsInitialize ConditionCheck: No Identity key found in blackboard.");
		return false; // No Identity key, cannot determine if it's initialized
	}

	// Check if the "Initialized" key exists in the blackboard
	bool HasInitialized = blackBoard.HasKey("Initialized");
	if (!HasInitialized)
	{
		std::cout << "IsInitialize ConditionCheck: No Initialized key found in blackboard." << std::endl;
		return true; // Entity is not initialized -> initialize action will be executed
	}

	bool isInitialized = blackBoard.GetValueAsBool("Initialized");
	if (isInitialized)
	{
		std::cout << "IsInitialize ConditionCheck: Entity is initialized." << std::endl;
		//Debug->Log("IsInitialize ConditionCheck: Entity is initialized.");
		return false; // Entity is initialized -> skip initialize action
	}
	else
	{
		std::cout << "IsInitialize ConditionCheck: Entity is not initialized." << std::endl;
		return true; // Entity is not initialized -> initialize action will be executed
	}
}
