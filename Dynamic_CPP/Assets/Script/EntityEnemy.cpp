#include "EntityEnemy.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
void EntityEnemy::Start()
{
	enemyBT =GetOwner()->GetComponent<BehaviorTreeComponent>();
	blackBoard = enemyBT->GetBlackBoard();

}

void EntityEnemy::Update(float tick)
{
	if (criticalMark != CriticalMark::None)
	{
		if (criticalMark == CriticalMark::P1)
		{
			auto obj = GameObject::Find("red");
			Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
			pos.y += 5;
			obj->m_transform.SetPosition(pos);
			obj->m_transform.UpdateWorldMatrix();
			auto effect = obj->GetComponent<EffectComponent>();
			if (effect)
			{
				effect->PlayEffectByName("red");
			}
		}
		else if (criticalMark == CriticalMark::P2)
		{

		}
	}



	if (isDead)
	{
		//effect
		Destroy();
	}
}

void EntityEnemy::SetCriticalMark(int playerIndex)
{
	if (playerIndex == 0)
	{
		criticalMark = CriticalMark::P1;
	}
	else if (playerIndex == 1)
	{
		criticalMark = CriticalMark::P2;
	}
}

void EntityEnemy::Attack(Entity* sender, int damage)
{

	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		//CurrHP - damae;
		if (player)
		{
			blackBoard->SetValueAsInt("Damage", damage);
			int playerIndex = player->playerIndex;
			m_currentHP -= std::max(damage, 0);
			if (m_currentHP >= 0)
			{
				isDead = true;
			}
		}
	}
}

