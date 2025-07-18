#include "IsDaed.h"
#include "pch.h"

bool IsDaed::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool useDead =  blackBoard.HasKey("eNorHP");
	if (useDead)
	{
		int hp = blackBoard.GetValueAsInt("eNorHP");
		if (hp <= 0)
		{
			return true; // Entity is dead
		}
	}

	return false;
}
