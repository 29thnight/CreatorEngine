#include "ActionCountCheck.h"
#include "pch.h"
#include "TBoss1.h"

bool ActionCountCheck::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");

	if (!hasIdentity) {
		return false;
	}
	std::string identity = blackBoard.GetValueAsString("Identity");
	if (identity == "Boss1")
	{
		TBoss1* script = m_owner->GetComponent<TBoss1>();
		if (script->actionCount < 3) { //일단 3회로 고정 -> 나중에 변수로 바꾸던가
			return true; //3회 이하일때만 true
		}
	}

	return false;
}
