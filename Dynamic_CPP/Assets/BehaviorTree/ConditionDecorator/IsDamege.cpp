#include "IsDamege.h"
#include "pch.h"
#include "TestEnemy.h"
#include "DebugLog.h"
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
			LOG("Atteck action already in progress.");
			//return NodeStatus::Running; // Continue running if already in attack state
			if (delayDamageTime > 0) {
				LOG("Delay damage action in progress for " << delayDamageTime << " seconds.");
				return true; // Continue running if delay damage action is in progress
			}
			else {
				LOG("Delay damage action not in progress.");
				return false; // No delay damage action, so condition not met
			}
		}		
	}
	
	LOG("IsDamege condition not met.");
	return false;
}
