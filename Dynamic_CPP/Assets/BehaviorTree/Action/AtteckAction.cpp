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
			//공격중이 아닐시 공격 시작
			script->isAttack = true;
            script->isAttackAnimation = true;
			script->m_animator->SetParameter("Attack", true);
            blackBoard.SetValueAsBool("IsAttacking", true);
            return NodeStatus::Running;
        }
        else {
			//공격중일시 에니메이션 종료 여부 확인
            if (isAttackAnimation) {
				//에니메이션이 실행중이면 계속 실행
				return NodeStatus::Running;
			}
            else 
            {
				//완료시 모든 변수 초기화하고 성공 반환
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
            //공격중이 아닐시 공격 시작
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
            //공격중일시 에니메이션 종료 여부 확인
            if (isAttackAnimation) {
                //에니메이션이 실행중이면 계속 실행
                return NodeStatus::Running;
            }
            else
            {
                //완료시 모든 변수 초기화하고 성공 반환
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
        //스테이트가 공격이 아닐시 첫 실행으로 인식 공격을 시작
        blackBoard.SetValueAsBool("IsAttacking", true); //--> 이건 에니메이션에서 false로 바꿔줘야함

        //// 3. 캐릭터의 움직임을 멈춥니다. //wait 에서 
        //if (auto* movement = m_owner->GetComponent<CharacterControllerComponent>())
        //{
        //    movement->Move(Mathf::Vector2(0.0f, 0.0f));
        //}

        //// 4. 애니메이터에 'Attack' 트리거를 전달하여 애니메이션을 재생시킵니다. // entityenemy 에서
        /*if (auto* animator = m_owner->GetComponent<Animator>())
        {
            animator->SetParameter("Attack", true);
        }*/

        // attack count를 증가 시켜 공격 시작
        blackBoard.SetValueAsInt("AttackCount", 1); // --> 이건 엔티티에서 공격 확인후 0으로 바꿔줘야함
        // 상태를 Atteck으로 변경
        blackBoard.SetValueAsString("State", "Atteck");  // --> 이건 BT가 쓰는 스테이트 

        // 5. BT에 Running 상태를 반환하여, 이 행동이 계속 실행 중임을 알립니다.
        return NodeStatus::Running;

    }
    else
    {
        //스테이트가 공격일시 에니메이션 종료 여부 확인
        // === 두 번째 Tick 이후: 공격 종료 확인 ===
        //LOG("AtteckAction Running...");
        //LOG("IsAttacking: " << blackBoard.GetValueAsBool("IsAttacking"));
        // Blackboard의 "IsAttacking" 플래그를 계속 확인합니다.
        if (blackBoard.GetValueAsBool("IsAttacking"))
        {
            // 아직 true라면, AttackBehavior가 신호를 보내지 않은 것이므로 애니메이션이 재생 중입니다.
            // 계속 Running 상태를 반환합니다.
            return NodeStatus::Running;
        }
        else
        {
            // 플래그가 false로 바뀌었다면, 애니메이션이 끝난 것입니다.
            // 1. 이 노드의 상태를 다시 '실행 전'으로 초기화합니다.
            //m_isActionRunning = false;

            // 2. BT에 Success를 반환하여 행동이 성공적으로 끝났음을 알립니다.
			//std::cout << "AtteckAction: Attack animation completed." << std::endl;
			//스테이트를 Idle로 변경
			blackBoard.SetValueAsString("State", "Idle");
            return NodeStatus::Success;
        }

    }
}
