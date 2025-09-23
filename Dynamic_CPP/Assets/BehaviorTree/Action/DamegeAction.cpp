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
				return NodeStatus::Success; //�˹鳡
			}
			else
			{
				auto forward = m_owner->m_transform.GetForward(); //���� ���⿡�� �и��Բ� ����
				auto controller = m_owner->GetComponent<CharacterControllerComponent>();
				controller->Move({ -forward.x , -forward.z });
				
				return NodeStatus::Running; //�˹���
			}
		}
	}
	//todo ::  ���� ������ �ð��̶� �˹�ȿ�� �־��

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
