#include "Phase1.h"
#include "pch.h"

bool Phase1::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");

	if (Identity != "Boss1") return false;

	int maxHp = blackBoard.GetValueAsInt("eHP");
	int hp = blackBoard.GetValueAsInt("eCurrentHP");

	float hpRatio = static_cast<float>(hp) / static_cast<float>(maxHp);

	if (hpRatio > 0.7f) {
		// 100% ~ 70% HP 
		return true; // Condition met
	}

	
	return false;
}
