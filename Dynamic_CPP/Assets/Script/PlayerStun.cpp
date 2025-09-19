#include "PlayerStun.h"
#include "pch.h"
#include "Player.h"
#include "AnimationController.h"
#include "Animator.h"
#include "Weapon.h"
void PlayerStun::Enter()
{
	if (m_player == nullptr)
	{
		AnimationController* ownerController = m_ownerController;
		if (ownerController)
		{
			Animator* P1ani = ownerController->GetOwner();
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
		m_player->ChangeState("Stun");
		m_player->m_curWeapon->SetEnabled(false);
		m_player->m_animator->SetUseLayer(1, false);
	}
}

void PlayerStun::Update(float deltaTime)
{
	if (m_player)
	{
		if (m_player->CheckResurrectionByOther() == true)
		{
			//tick�÷��� ���ʴ� �ѹ��� ��Ȱ������ �ø���
			m_player->ResurrectionElapsedTime += deltaTime;
			if (m_player->ResurrectionElapsedTime >= m_player->ResurrectionTime)
			{
				m_player->Resurrection();
			}
		}
		else  //�ƴҋ��� ������ ������?
		{

		}
	}
}

void PlayerStun::Exit()
{
	if (m_player)
	{
		m_player->ChangeState("Idle");
		m_player->m_curWeapon->SetEnabled(true);
		m_player->m_animator->SetUseLayer(1, true);
	}
}
