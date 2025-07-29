#include "TeleportAction.h"
#include "pch.h"

NodeStatus TeleportAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//��Ÿ�� ���� ��� �����ϰ� ���� ���� ���� ���� �����Ƿ� ���۵Ǵ� ����
	auto Identity = blackBoard.GetValueAsString("Identity");
	if (Identity.empty() || Identity != "Mage")
	{
		return NodeStatus::Failure; // Identity�� ���ų� "Mage"�� �ƴ� ��� ���� ��ȯ
	}

	auto State = blackBoard.GetValueAsString("State");

	if (State == "Teleport")
	{

		// �ڷ���Ʈ ���� ��
		// �ڷ���Ʈ ������ �����ؾ� �մϴ�.
		// ���ϸ��̼��� ��� �Ǵ� ��Ȳ���� Ư�� Ÿ�ֿ̹� ���缭 �ڷ���Ʈ ��ġ�� �����ϰ� �̵��ϴ� ������ �����ؾ� �մϴ�.

		std::cout << "Mage Teleport: Teleporting to a new location" << std::endl;
		// �ڷ���Ʈ �� ��Ÿ�� ����
		float coolTime = blackBoard.GetValueAsFloat("eTPCooldown");
		blackBoard.SetValueAsFloat("TeleportCooldown", coolTime); // �ڷ���Ʈ ��Ÿ�� ����
		
	}
	else {
		State = "Teleport"; // ���¸� �ڷ���Ʈ�� ����
		blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
	}



	return NodeStatus::Success;
}
