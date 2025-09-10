#include "TestConCec.h"
#include "pch.h"
#include "DebugLog.h"
bool TestConCec::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool condition = blackBoard.HasKey("Cond1");
	if (condition) {
		int a = blackBoard.GetValueAsInt("Cond1");
		if (a>10)
		{
			LOG("ConditionCheck: a is greater than 10");
			return false;
		}
		else
		{
			LOG("ConditionCheck: a is not greater than 10");
		}
	}

	return true;
}
