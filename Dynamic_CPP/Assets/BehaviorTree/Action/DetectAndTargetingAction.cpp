#include "DetectAndTargetingAction.h"
#include "pch.h"
#include "TBoss1.h"

NodeStatus DetectAndTargetingAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool hasIdentity = blackBoard.HasKey("Identity");
	if (!hasIdentity) {
		return NodeStatus::Failure; // Identity Ű�� ������ ����
	}
	std::string identity = blackBoard.GetValueAsString("Identity");
	if (identity != "Boss1") {
		return NodeStatus::Failure; // Identity�� "Boss1"�� �ƴϸ� ����
	}
	TBoss1* script = m_owner->GetComponent<TBoss1>();
	script->SelectTarget();

	return NodeStatus::Success;
}
