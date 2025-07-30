#include "Phase3.h"
#include "pch.h"

bool Phase3::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");

	if (Identity != "Boss1") return false;

	int maxHp = blackBoard.GetValueAsInt("eHP");
	int hp = blackBoard.GetValueAsInt("eCurrentHP");

	float hpRatio = static_cast<float>(hp) / static_cast<float>(maxHp);

	if (hpRatio <= 0.7 && hpRatio > 0.3) {
		// 70% ~ 30% HP
		return true; // Condition met
	}

	return false; // Condition not met
}
