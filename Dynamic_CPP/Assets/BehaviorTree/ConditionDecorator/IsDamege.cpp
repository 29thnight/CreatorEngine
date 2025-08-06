#include "IsDamege.h"
#include "pch.h"
#include "TestEnemy.h"
bool IsDamege::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool isAnime = blackBoard.HasKey("AnimeState");
	bool isDelayDamageAction = blackBoard.HasKey("DelayDamageAction");
	float delayDamageTime = 0.0f;

	bool hasDamage = blackBoard.HasKey("Damage");
	int damage=0;
	if (hasDamage)
	{
		damage = blackBoard.GetValueAsInt("Damage");
	}
	else
	{
		return false;
	}


	if (damage > 0) {
		return true;
	}


	if (isDelayDamageAction)
	{
		delayDamageTime = blackBoard.GetValueAsFloat("DelayDamageAction");
	}

	if (isAnime)
	{
		std::string state = blackBoard.GetValueAsString("AnimeState");
		if (state == "Damege")
		{
			std::cout << "Atteck action already in progress." << std::endl;
			//return NodeStatus::Running; // Continue running if already in attack state
			if (delayDamageTime > 0) {
				std::cout << "Delay damage action in progress for " << delayDamageTime << " seconds." << std::endl;
				return true; // Continue running if delay damage action is in progress
			}
			else {
				std::cout << "Delay damage action not in progress." << std::endl;
				return false; // No delay damage action, so condition not met
			}
		}		
	}
	
	std::cout << "IsDamege condition not met." << std::endl;
	return false;
}
