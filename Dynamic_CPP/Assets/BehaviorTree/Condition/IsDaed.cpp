#include "IsDaed.h"
#include "pch.h"
#include "DebugLog.h"
bool IsDaed::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool HasIdentity = blackBoard.HasKey("Identity");
	
	if (!HasIdentity)
	{
		//LOG("IsDaed ConditionCheck: No Identity key found in blackboard.");
		return false; // No Identity key, cannot determine if it's a dead entity
	}

	bool useDead = blackBoard.HasKey("HP");
	bool started = blackBoard.HasKey("CurrHP");
	if (useDead)
	{
		int hp = blackBoard.GetValueAsInt("CurrHP");
		if (hp <= 0)
		{
			return true; // Entity is dead
		}
		else 
		{
			//LOG("IsDaed ConditionCheck: Entity is alive, HP: " << hp);
			return false; // Entity is alive
		}
	}
	//LOG("IsDaed ConditionCheck: Entity is not dead, HP: " << blackBoard.GetValueAsInt("HP"));
	return false;
}
