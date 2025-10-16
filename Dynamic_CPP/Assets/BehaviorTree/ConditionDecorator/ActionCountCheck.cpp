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
		if (script->actionCount < 3) { //�ϴ� 3ȸ�� ���� -> ���߿� ������ �ٲٴ���
			return true; //3ȸ �����϶��� true
		}
	}

	return false;
}
