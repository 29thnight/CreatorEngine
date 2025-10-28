#include "Phase1.h"
#include "pch.h"
#include "DebugLog.h"
bool Phase1::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	auto Identity = blackBoard.GetValueAsString("Identity");

	if (Identity != "Boss1") return false;

	int maxHp = blackBoard.GetValueAsInt("MaxHP");
	int hp = blackBoard.GetValueAsInt("CurrHP");

	float hpRatio = static_cast<float>(hp) / static_cast<float>(maxHp);


	//LOG("HPRatio is : "<< hpRatio << " Phase1 is " << ((bool)(hpRatio > 0.5f) ? "True" : "False"));
	if (hpRatio > 0.7f) {
		// 100% ~ 70% HP 

		return true; // Condition met
	}

	
	return false;
}
