#include "PlayerAttackAH.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "AnimationController.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "EffectComponent.h"
#include "SceneManager.h"
#include "Socket.h"

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
		auto controller = m_player->GetOwner()->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0,0 });
	}
	if (!eft)
	{
		eft = SceneManagers->GetActiveScene()->CreateGameObject("asd").get();
		eft->AddComponent<EffectComponent>()->Awake();
	}
	eft->GetComponent<EffectComponent>()->ChangeEffect("ss1");
}

void PlayerAttackAH::Update(float deltaTime)
{
	eft->m_transform.SetPosition(m_player->handSocket->transform.GetLocalMatrix().r[3]);
	if (m_player)
	{
		m_player->MeleeAttack();
	}

	std::cout << "Attack behavior Test" << std::endl;
}

void PlayerAttackAH::Exit()
{
	m_player->isAttacking = false;
}
