#include "AtteckAction.h"
#include "pch.h"
#include "DebugLog.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	bool hasAttackState = blackBoard.HasKey("IsAttacking");
	bool hasState = blackBoard.HasKey("State");
	bool isActionRunning = false;
	std::string state ="";

    if (hasAttackState) {
        isActionRunning = blackBoard.GetValueAsBool("IsAttacking");
    }

    if (hasState) {
		state = blackBoard.GetValueAsString("State");
    }

    if (state != "Atteck")
    {
        //������Ʈ�� ������ �ƴҽ� ù �������� �ν� ������ ����
        blackBoard.SetValueAsBool("IsAttacking", true); //--> �̰� ���ϸ��̼ǿ��� false�� �ٲ������

        //// 3. ĳ������ �������� ����ϴ�. //wait ���� 
        //if (auto* movement = m_owner->GetComponent<CharacterControllerComponent>())
        //{
        //    movement->Move(Mathf::Vector2(0.0f, 0.0f));
        //}

        //// 4. �ִϸ����Ϳ� 'Attack' Ʈ���Ÿ� �����Ͽ� �ִϸ��̼��� �����ŵ�ϴ�. // entityenemy ����
        //if (auto* animator = m_owner->GetComponent<Animator>())
        //{
        //    animator->SetParameter("Attack", true);
        //}

        // attack count�� ���� ���� ���� ����
        blackBoard.SetValueAsInt("AttackCount", 1); // --> �̰� ��ƼƼ���� ���� Ȯ���� 0���� �ٲ������
        // ���¸� Atteck���� ����
        blackBoard.SetValueAsString("State", "Atteck");  // --> �̰� BT�� ���� ������Ʈ 

        // 5. BT�� Running ���¸� ��ȯ�Ͽ�, �� �ൿ�� ��� ���� ������ �˸��ϴ�.
        return NodeStatus::Running;

    }
    else
    {
        //������Ʈ�� �����Ͻ� ���ϸ��̼� ���� ���� Ȯ��
        // === �� ��° Tick ����: ���� ���� Ȯ�� ===
        LOG("AtteckAction Running...");
        LOG("IsAttacking: " << blackBoard.GetValueAsBool("IsAttacking"));
        // Blackboard�� "IsAttacking" �÷��׸� ��� Ȯ���մϴ�.
        if (blackBoard.GetValueAsBool("IsAttacking"))
        {
            // ���� true���, AttackBehavior�� ��ȣ�� ������ ���� ���̹Ƿ� �ִϸ��̼��� ��� ���Դϴ�.
            // ��� Running ���¸� ��ȯ�մϴ�.
            return NodeStatus::Running;
        }
        else
        {
            // �÷��װ� false�� �ٲ���ٸ�, �ִϸ��̼��� ���� ���Դϴ�.
            // 1. �� ����� ���¸� �ٽ� '���� ��'���� �ʱ�ȭ�մϴ�.
            //m_isActionRunning = false;

            // 2. BT�� Success�� ��ȯ�Ͽ� �ൿ�� ���������� �������� �˸��ϴ�.
			LOG("AtteckAction: Attack animation completed.");
			//������Ʈ�� Idle�� ����
			blackBoard.SetValueAsString("State", "Idle");
            return NodeStatus::Success;
        }

    }
}
