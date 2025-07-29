#include "IsDaed.h"
#include "pch.h"

bool IsDaed::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool useDead = blackBoard.HasKey("eHP");
	if (useDead)
	{
		int hp = blackBoard.GetValueAsInt("eHP");
		if (hp <= 0)
		{
			return true; // Entity is dead
		}
	}

	return false;
}
