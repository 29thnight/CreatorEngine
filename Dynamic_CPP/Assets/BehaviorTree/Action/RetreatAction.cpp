#include "RetreatAction.h"
#include "pch.h"

NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//todo ���� ġ�� ��� ��������? ���� ����� �÷��̾�� �ݴ��?
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return NodeStatus::Failure; // Identity�� ���ų� "Mage"�� �ƴ� ��� ���� ��ȯ
	}
	auto State = blackBoard.GetValueAsString("State");
	if (State == "Retreat")
	{
		// ���� ���� ��
		// ���� ������ �����ؾ� �մϴ�.
		// ���ϸ��̼��� ��� �Ǵ� ��Ȳ���� Ư�� Ÿ�ֿ̹� ���缭 �������� �������� �̵��ϴ� ������ �����ؾ� �մϴ�.
		std::cout << "Mage Retreat: Retreating to a safe location" << std::endl;
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
