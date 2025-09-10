#include "Phase2.h"
#include "pch.h"
#include "DebugLog.h"
bool Phase2::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");

	if (Identity != "Boss1") return false;

	int maxHp = blackBoard.GetValueAsInt("HP");
	int hp = blackBoard.GetValueAsInt("CurrHP");

	float hpRatio = static_cast<float>(hp) / static_cast<float>(maxHp);

	LOG("HPRatio is : " << hpRatio << " Phase2 is " << ((bool)(hpRatio <= 0.7 && hpRatio > 0.3)  ? "True" : "False"));
	if (hpRatio <= 0.7 && hpRatio > 0.3) {
		// 70% ~ 30% HP
		return true; // Condition met
	}
	
	return false; // Condition not met
}
