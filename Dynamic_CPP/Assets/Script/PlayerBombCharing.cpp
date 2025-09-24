#include "PlayerBombCharing.h"
#include "pch.h"
#include "Animator.h"
#include "Player.h"
#include "CharacterControllerComponent.h"
void PlayerBombCharing::Enter()
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
		m_player->OnMoveBomb = true;
		m_player->onBombIndicate = true;
		m_player->ChangeState("Attack");
		m_player->isAttacking = true;
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
		if (m_player->m_animator)
			m_player->m_animator->SetParameter("OnMove", false);
		m_player->m_animator->SetUseLayer(1, false);
	}

}

void PlayerBombCharing::Update(float deltaTime)
{
}

void PlayerBombCharing::Exit()
{
	/*if (m_player)
	{
		m_player->ChangeState("Idle");
		m_player->isAttacking = false;
		m_player->sucessAttack = true;
	}*/
}
