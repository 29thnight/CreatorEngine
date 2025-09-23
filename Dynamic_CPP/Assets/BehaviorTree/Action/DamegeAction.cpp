#include "DamegeAction.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "CharacterControllerComponent.h"
#include "DebugLog.h"
NodeStatus DamegeAction::Tick(float deltatime, BlackBoard& blackBoard)
{
	
	int damage = blackBoard.GetValueAsInt("Damage");
	int currHP = blackBoard.GetValueAsInt("CurrHP");

	currHP -=damage;

	blackBoard.SetValueAsInt("CurrHP", currHP);
	blackBoard.SetValueAsInt("Damage", 0);

	EntityEnemy* enemy = m_owner->GetComponent<EntityEnemy>();

	if (enemy)
	{
		if (enemy->isKnockBack)
		{
			enemy->KnockBackElapsedTime += deltatime;
			if (enemy->KnockBackElapsedTime >= enemy->KnockBackTime)
			{
				enemy->isKnockBack = false;
				enemy->KnockBackElapsedTime = 0.f;
				m_owner->GetComponent<CharacterControllerComponent>()->StopForcedMove();
				return NodeStatus::Success; //넉백끝
			}
			else
			{
				auto forward = m_owner->m_transform.GetForward(); //맞은 방향에서 밀리게끔 수정
				auto controller = m_owner->GetComponent<CharacterControllerComponent>();
				controller->Move({ -forward.x , -forward.z });
				
				return NodeStatus::Running; //넉백중
			}
		}
	}
	//todo ::  엑션 보여줄 시간이랑 넉백효과 넣어라

	/*bool isDelayDamageAction = blackBoard.HasKey("DelayDamageAction");
	float delayDamageTime = 0.0f;
	if (isDelayDamageAction)
	{
		delayDamageTime = blackBoard.GetValueAsFloat("DelayDamageAction");
		delayDamageTime -= deltatime;
		blackBoard.SetValueAsFloat("DelayDamageAction", delayDamageTime);
	}
	else {
		
		
		
		
		"No delay damage action found.");
		blackBoard.SetValueAsFloat("DelayDamageAction", 2.0f);
	}

	LOG("Damege action executed successfully.");*/
	//return NodeStatus::Success;
	return NodeStatus::Running;
}
