#include "AtteckAction.h"
#include "pch.h"
#include "EntityMonsterA.h"
#include "TestMonsterB.h"
#include "Animator.h"

NodeStatus AtteckAction::Tick(float deltatime, BlackBoard& blackBoard)
{

    bool hasIdentity = blackBoard.HasKey("Identity");

    std::string identity = "";
    if (hasIdentity)
    {
        identity = blackBoard.GetValueAsString("Identity");
    }

    if (identity == "MonsterNomal")
    {
        EntityMonsterA* script = m_owner->GetComponent<EntityMonsterA>();
		bool isAttack = script->isAttack;
		bool isAttackAnimation = script->isAttackAnimation;
        if (!isAttack) {
			//�������� �ƴҽ� ���� ����
			script->isAttack = true;
            script->isAttackAnimation = true;
			script->m_animator->SetParameter("Attack", true);
            blackBoard.SetValueAsBool("IsAttacking", true);
            return NodeStatus::Running;
        }
        else {
			//�������Ͻ� ���ϸ��̼� ���� ���� Ȯ��
            if (isAttackAnimation) {
				//���ϸ��̼��� �������̸� ��� ����
				return NodeStatus::Running;
			}
            else 
            {
				//�Ϸ�� ��� ���� �ʱ�ȭ�ϰ� ���� ��ȯ
				script->isAttack = false;
				script->isAttackAnimation = false;
				script->m_state = "Idle";
				return NodeStatus::Success;
            }
        }
    }

    if (identity == "MonsterRange") {
        


        TestMonsterB* script = m_owner->GetComponent<TestMonsterB>();
        bool isAttack = script->isAttack;
        bool isAttackAnimation = script->isAttackAnimation;
        bool isMelee = script->isMelee;
        if (!isAttack) {
            //�������� �ƴҽ� ���� ����
            script->isAttack = true;
            script->isAttackAnimation = true;
            if (isMelee) 
            {
                script->m_animator->SetParameter("Attack", true);
            }
            else 
            {
                script->m_animator->SetParameter("RangeAttack", true);
            }
            blackBoard.SetValueAsBool("IsAttacking", true);
            return NodeStatus::Running;
        }
        else {
            //�������Ͻ� ���ϸ��̼� ���� ���� Ȯ��
            if (isAttackAnimation) {
                //���ϸ��̼��� �������̸� ��� ����
                return NodeStatus::Running;
            }
            else
            {
                //�Ϸ�� ��� ���� �ʱ�ȭ�ϰ� ���� ��ȯ
                script->isAttack = false;
                script->isAttackAnimation = false;
                script->m_state = "Idle";
                return NodeStatus::Success;
            }
        }
    }


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
        /*if (auto* animator = m_owner->GetComponent<Animator>())
        {
            animator->SetParameter("Attack", true);
        }*/

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
        //LOG("AtteckAction Running...");
        //LOG("IsAttacking: " << blackBoard.GetValueAsBool("IsAttacking"));
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
			//std::cout << "AtteckAction: Attack animation completed." << std::endl;
			//������Ʈ�� Idle�� ����
			blackBoard.SetValueAsString("State", "Idle");
            return NodeStatus::Success;
        }

    }
}
