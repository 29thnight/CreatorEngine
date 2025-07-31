#include "EntityEnemy.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"

void EntityEnemy::Start()
{
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
		if (player)
		{
			int playerIndex = player->playerIndex;
			m_currentHP -= std::max(damage, 0);
		}
	}
}

