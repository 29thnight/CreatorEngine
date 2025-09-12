#include "PlayerRangeAttack.h"
#include "pch.h"
#include "Animator.h"
#include "Player.h"
#include "CharacterControllerComponent.h"
#include "Weapon.h"
void PlayerRangeAttack::Enter()
{
	if (m_player == nullptr)
	{
		AnimationController* P1uppercontroller = m_ownerController;
		if (P1uppercontroller)
		{
			Animator* P1ani = P1uppercontroller->GetOwner();
			if (P1ani)
			{
				GameObject* P1 = P1ani->GetOwner();
				if (P1)
				{
					GameObject* parent = GameObject::FindIndex(P1->m_parentIndex);
					if (parent)
					{
						m_player = parent->GetComponent<Player>();
					}
				}
			}
		}
	}

	if (m_player)
	{
		m_player->isAttacking = true;
		m_player->DropCatchItem();
		m_player->m_animator->SetUseLayer(1, false);
		m_player->RangeAttack();
	}
}

void PlayerRangeAttack::Update(float deltaTime)
{
	if (m_player)
	{
		m_player->isAttacking = true;
		auto controller = m_player->GetOwner()->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0,0 });
	}
}

void PlayerRangeAttack::Exit()
{
	if (m_player)
	{
		if (!m_player->m_curWeapon->IsBasic())
		{
			m_player->m_curWeapon->DecreaseDur(m_player->isChargeAttack);
			m_player->m_UpdateDurabilityEvent.Broadcast(m_player->m_curWeapon, m_player->m_weaponIndex);
		}
		m_player->isAttacking = false;
		m_player->sucessAttack = true;

	}
}
