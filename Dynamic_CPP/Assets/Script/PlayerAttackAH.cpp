#include "PlayerAttackAH.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "AnimationController.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "Socket.h"
#include "EffectComponent.h"

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
	if (!m_effect)
	{
		m_effect = SceneManagers->GetActiveScene()->CreateGameObject("asd").get();
		m_effect->AddComponent<EffectComponent>()->Awake();

	}

	if (m_player)
	{
		m_effect->m_transform.SetPosition(m_player->handSocket->transform.GetLocalMatrix().r[3]);
	}

	m_effect->GetComponent<EffectComponent>()->PlayEffectByName("1234");
}

void PlayerAttackAH::Update(float deltaTime)
{
	m_effect->m_transform.SetPosition(m_player->handSocket->transform.GetLocalMatrix().r[3]);
	if (m_player)
	{
		m_player->MeleeAttack();
	}

	std::cout << "Attack behavior Test" << std::endl;
}

void PlayerAttackAH::Exit()
{
	m_effect->GetComponent<EffectComponent>()->StopEffect();
	m_player->isAttacking = false;

}
