#include "TestConCec.h"
#include "pch.h"

bool TestConCec::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool condition = blackBoard.HasKey("Cond1");
	if (condition) {
		int a = blackBoard.GetValueAsInt("Cond1");
		if (a>10)
		{
			std::cout << "ConditionCheck: a is greater than 10" << std::endl;
			return false;
		}
		else
		{
			std::cout << "ConditionCheck: a is not greater than 10" << std::endl;
		}
	}

	return true;
}
