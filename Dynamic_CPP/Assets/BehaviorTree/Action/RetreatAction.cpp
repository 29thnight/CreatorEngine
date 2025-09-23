#include "RetreatAction.h"
#include "pch.h"
#include "DebugLog.h"
NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	auto State = blackBoard.GetValueAsString("State");
	if (State == "Retreat")
	{
		// ���� ���� ��
		// ���� ������ �����ؾ� �մϴ�.
		// ���ϸ��̼��� ��� �Ǵ� ��Ȳ���� Ư�� Ÿ�ֿ̹� ���缭 �������� �������� �̵��ϴ� ������ �����ؾ� �մϴ�.
		LOG("Mage Retreat: Retreating to a safe location");
		// ���� �� ��Ÿ�� ����
		float coolTime = blackBoard.GetValueAsFloat("eRetreatCooldown");
		blackBoard.SetValueAsFloat("RetreatCooldown", coolTime); // ���� ��Ÿ�� ����
	}
	else {
		State = "Retreat"; // ���¸� �������� ����
		blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
	}



	return NodeStatus::Success;
}
