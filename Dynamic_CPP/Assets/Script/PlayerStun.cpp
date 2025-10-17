#include "PlayerStun.h"
#include "pch.h"
#include "Player.h"
#include "AnimationController.h"
#include "Animator.h"
#include "Weapon.h"
#include "CharacterControllerComponent.h"
#include "EffectComponent.h"
#include "Socket.h"
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
		m_player->DropCatchItem();
		m_player->onBombIndicate = false;
		m_player->m_curWeapon->SetEnabled(false);
		m_player->m_animator->SetUseLayer(1, false);
		if (m_player->stunEffect)
		{
			m_player->stunEffect->Apply();
		}
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
	}
}

void PlayerStun::Update(float deltaTime)
{
	if (m_player)
	{

		if (m_player->stunEffect)
		{
			Mathf::Matrix left = m_player->leftEarSokcet->transform.GetLocalMatrix();
			Mathf::Vector3 leftPos = left.Translation();

			Mathf::Matrix right = m_player->rightEarSokcet->transform.GetLocalMatrix();
			Mathf::Vector3 rightPos = right.Translation();


			Mathf::Vector3 middlePos = (leftPos + rightPos) * 0.5f;
			middlePos.y += 0.6;
			m_player->stunObj->GetComponent<Transform>()->SetPosition(middlePos);
		}


		if (m_player->CheckResurrectionByOther() == true)
		{
			//tick올려서 몇초당 한번씩 부활게이지 올리기
			m_player->ResurrectionElapsedTime += deltaTime;
			if (m_player->ResurrectionElapsedTime >= m_player->ResurrectionTime)
			{
				m_player->Resurrection();
			}
		}
		else  //아닐떄는 게이지 내리기?
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
		if (m_player->stunEffect)
		{
			m_player->stunEffect->StopEffect();
		}
		//m_player->m_animator->SetUseLayer(1, true);
	}
}
