#include "PlayerAttackAH.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "AnimationController.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
void PlayerAttackAH::Enter()
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
					std::cout << P1->m_name.ToString() << std::endl;
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
		m_player->AttackTarget.clear();
	}
}

void PlayerAttackAH::Update(float deltaTime)
{
	
	if (m_player && m_player->startRay)
	{
		m_player->MeleeAttack();
	}

	auto controller = m_player->GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->Move({ 0,0 });
}

void PlayerAttackAH::Exit()
{
	m_player->isAttacking = false;
	/*if (m_player->m_comboCount < 2)
	{
		m_player->m_comboCount++;
		m_player->m_comboElapsedTime = 0.f;
	}
	else
	{
		m_player->m_comboCount = 0;
		m_player->m_comboElapsedTime = 0.f;
	}*/
	std::cout << "attack end" << std::endl;
}
