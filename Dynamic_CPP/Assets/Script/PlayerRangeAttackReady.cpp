#include "PlayerRangeAttackReady.h"
#include "pch.h"
#include "Animator.h"
#include "Player.h"
#include "CharacterControllerComponent.h"
#include "Weapon.h"
void PlayerRangeAttackReady::Enter()
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
		m_player->DropCatchItem();
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
		//m_player->m_animator->SetUseLayer(1, false);
		m_player->RangeAttack();
	}

}

void PlayerRangeAttackReady::Update(float deltaTime)
{
}

void PlayerRangeAttackReady::Exit()
{
	if (m_player)
	{
		m_player->ChangeState("Idle");
		//m_player->isAttacking = false; //레인지어택 1-1 , 1-2도중 피격들어오면 무기내구도 소모후 공격처리 들어가야함
		//m_player->sucessAttack = true;

	}
}
