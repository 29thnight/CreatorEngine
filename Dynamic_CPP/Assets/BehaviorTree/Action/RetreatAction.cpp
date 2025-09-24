#include "RetreatAction.h"
#include "pch.h"
#include "EntityEleteMonster.h"
#include "DebugLog.h"
NodeStatus RetreatAction::Tick(float deltatime, BlackBoard& blackBoard)
{

	//����ǿ��� �˻��ѰŴ� �ǳʶ��� �����Ǹ� ������ ���� 
	bool hasState = blackBoard.HasKey("State");
	std::string State = "";
	if (hasState) {
		State = blackBoard.GetValueAsString("State");
	}


	EntityEleteMonster* script = m_owner->GetComponent< EntityEleteMonster>();

	//����ġ�µ� �󸶳� ����ġ�� ������ ������ ���� �����ļ� 
	//���� ����ġ�� �ʰ� �ο쳪? �ƴϸ� ƽ���� ���� ������ ��������� ��ģ��ŭ �̵�?
	//�ϴ� ��ģ��ŭ �̵��ϰ� ��ȸ�� ����� 

	if (State == "Retreat")
	{
		if (script->m_isRetreat) {
			// ���� ���� ��
			// ���� ������ �����ؾ� �մϴ�.
			// ���ϸ��̼��� ��� �Ǵ� ��Ȳ���� Ư�� Ÿ�ֿ̹� ���缭 �������� �������� �̵��ϴ� ������ �����ؾ� �մϴ�.
			LOG("Mage Retreat: Retreating to a safe location");
			return NodeStatus::Running;
		}
		else 
		{
			// ���� �� ��Ÿ�� ����
			State = "Idle"; //���� �ʱ�ȭ
			blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
			float cooldown = script->m_retreatCoolTime;
			blackBoard.SetValueAsFloat("ReteatCooldown",cooldown); // ��Ÿ�� ����
			return NodeStatus::Success;
		}
	}
	else {
		State = "Retreat"; // ���¸� �������� ����
		blackBoard.SetValueAsString("State", State); // ���¸� �����忡 ����
		script->StartRetreat();
		return NodeStatus::Running;
	}

	return NodeStatus::Success;
}
