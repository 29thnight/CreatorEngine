#include "IsDaed.h"
#include "pch.h"

bool IsDaed::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool HasIdentity = blackBoard.HasKey("Identity");
	
	if (!HasIdentity)
	{
		std::cout << "IsDaed ConditionCheck: No Identity key found in blackboard." << std::endl;
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
			std::cout << "IsDaed ConditionCheck: Entity is alive, HP: " << hp << std::endl;
			return false; // Entity is alive
		}
	}
	std::cout << "IsDaed ConditionCheck: Entity is not dead, HP: " << blackBoard.GetValueAsInt("HP") << std::endl;
	return false;
}
