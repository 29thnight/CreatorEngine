#include "IsBossAtteck.h"
#include "pch.h"
#include "DebugLog.h"
bool IsBossAtteck::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{

	bool isProcessed = blackBoard.HasKey("IdleTime");
	float idleTime; 
	if (isProcessed) {
		// atteack is add IdleTime to ActionNode
		idleTime = blackBoard.GetValueAsFloat("IdleTime");
		LOG("IdleTime is : " << idleTime);
	}
	else 
	{
		// If IdleTime is not found, boss is not yet attacking
		idleTime = 0.0f; // Default value if not found
		LOG("IdleTime is not found, default to 0.0f");
	}

	if (idleTime > 0) {
		LOG("Boss is Idle, not attacking");
		return false; // Boss is Idle, not attacking
	}

	LOG("Boss is send attack node");
	return true; // Boss is attacking
}
