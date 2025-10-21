#include "TeleportAction.h"
#include "pch.h"
#include "EntityEleteMonster.h"
#include "DebugLog.h"
NodeStatus TeleportAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	//��Ÿ�� ���� ��� �����ϰ� ���� ���� ���� ���� �����Ƿ� ���۵Ǵ� ����
	//����ǿ��� �˻��ѰŴ� �ǳʶ��� �����Ǹ� ������ ���� 
	bool hasState = blackBoard.HasKey("State");
	std::string State = "";
	if (hasState) {
		State = blackBoard.GetValueAsString("State");
	}


	EntityEleteMonster* script = m_owner->GetComponent< EntityEleteMonster>();
	bool action = script->m_isTeleport;

	//�ϴ� ���ϸ��̼� ó�� ����Ʈ ó�� �̷��� �� �ǳʶٰ� �ϴ� �����̵� ���Ѻ���
	if (State == "Teleport") {
		if (action) {//�ൿ ���̸� 
			return NodeStatus::Running;
		}
		else //�ൿ �������� ��� ���·� ����
		{
			State = "Idle";
			blackBoard.SetValueAsString("State", State);
			script->m_isTeleport = false;
			script->m_posset = false;
			return NodeStatus::Success;
		}
	}
	else //ù ���Խ� �ڷ���Ʈ ����
	{
		State = "Teleport";
		blackBoard.SetValueAsString("State", State);
		if (script) {
			script->StartTeleport();
		}
		return NodeStatus::Running;
	}

	//if (State == "Teleport")
	//{

	//	// �ڷ���Ʈ ���� ��
	//	// �ڷ���Ʈ ������ �����ؾ� �մϴ�.
	//	// ���ϸ��̼��� ��� �Ǵ� ��Ȳ���� Ư�� Ÿ�ֿ̹� ���缭 �ڷ���Ʈ ��ġ�� �����ϰ� �̵��ϴ� ������ �����ؾ� �մϴ�.

	//	LOG("Mage Teleport: Teleporting to a new location");
	//	// �ڷ���Ʈ �� ��Ÿ�� ����
	//	float coolTime = blackBoard.GetValueAsFloat("eTPCooldown");
	//	blackBoard.SetValueAsFloat("TeleportCooldown", coolTime); // �ڷ���Ʈ ��Ÿ�� ����
	//	
	//}
	//else {
	//	State = "Teleport"; // ���¸� �ڷ���Ʈ�� ����
	//	blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
	//}



	return NodeStatus::Success;
}
