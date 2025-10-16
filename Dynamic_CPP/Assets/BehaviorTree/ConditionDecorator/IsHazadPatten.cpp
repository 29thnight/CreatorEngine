#include "IsHazadPatten.h"
#include "pch.h"
#include "TBoss1.h"

bool IsHazadPatten::ConditionCheck(float deltatime, const BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");

	if (!hasIdentity) {
		return false;
	}
	std::string identity = blackBoard.GetValueAsString("Identity");
	if (identity == "Boss1")
	{
		TBoss1* script = m_owner->GetComponent<TBoss1>();
		float interval = script->hazardInterval;
		if (interval < script->hazardTimer) {
			return true;
		}
	}

	return false;
}
