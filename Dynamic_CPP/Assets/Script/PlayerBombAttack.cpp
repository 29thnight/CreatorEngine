#include "PlayerBombAttack.h"
#include "pch.h"
#include "Animator.h"
#include "Player.h"
#include "CharacterControllerComponent.h"
#include "Weapon.h"
void PlayerBombAttack::Enter()
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
		m_player->ChangeState("Attack");
		m_player->isAttacking = true;
		m_player->m_animator->SetUseLayer(1, false);
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
		//m_player->m_animator->SetUseLayer(1, false);
	}

}

void PlayerBombAttack::Update(float deltaTime)
{

}

void PlayerBombAttack::Exit()
{
	if (m_player)
	{
		if (!m_player->m_curWeapon->IsBasic())
		{
			m_player->m_curWeapon->DecreaseDur(m_player->isChargeAttack);
			m_player->m_UpdateDurabilityEvent.Broadcast(m_player->m_curWeapon, m_player->m_weaponIndex);
		}
		m_player->ChangeState("Idle");
		m_player->isAttacking = false;
		m_player->sucessAttack = true;
		m_player->m_curWeapon->SetEnabled(true);
	}
}
