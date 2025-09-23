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
#include "DebugLog.h"
#include "Weapon.h"
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
		m_player->DropCatchItem();
		m_player->AttackTarget.clear();
		m_player->ChangeState("Attack");
		m_player->isAttacking = true;
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
		m_player->m_animator->SetUseLayer(1, false);
	}
	/*if (!eft)
	{
		eft = SceneManagers->GetActiveScene()->CreateGameObject("asd").get();
		eft->AddComponent<EffectComponent>()->Awake();
	}
	eft->GetComponent<EffectComponent>()->ChangeEffect("test2");*/
}

void PlayerAttackAH::Update(float deltaTime)
{
	
	if (m_player && m_player->startRay)
	{
		//eft->m_transform.SetPosition(m_player->handSocket->transform.GetLocalMatrix().r[3]); //keyframe �̺�Ʈ�� 
	}

	if (m_player && m_player->startRay)
	{
		m_player->MeleeAttack();
	}
	m_player->m_comboElapsedTime = 0;
}

void PlayerAttackAH::Exit()
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
		m_player->m_comboElapsedTime = 0;
		m_player->ChangeState("Idle");

	}
	
}
