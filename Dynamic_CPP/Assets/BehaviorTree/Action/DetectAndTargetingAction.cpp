#include "DetectAndTargetingAction.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus DetectAndTargetingAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");
	if (!hasIdentity) {
		return NodeStatus::Failure; // Identity 키가 없으면 실패
	}
	std::string identity = blackBoard.GetValueAsString("Identity");
	if (identity != "Boss1") {
		return NodeStatus::Failure; // Identity가 "Boss1"이 아니면 실패
	}
	TBoss1* script = m_owner->GetComponent<TBoss1>();
	script->SelectTarget();

	return NodeStatus::Success;
}
