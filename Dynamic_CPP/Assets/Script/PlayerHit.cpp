#include "PlayerHit.h"
#include "pch.h"
#include "Player.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
void PlayerHit::Enter()
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
		m_player->ChangeState("Hit");
		m_player->m_animator->SetUseLayer(1, false);
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
	}
}

void PlayerHit::Update(float deltaTime)
{
}

void PlayerHit::Exit()
{
	if (m_player)
	{
		m_player->ChangeState("Idle");
		m_player->m_animator->SetUseLayer(1, true);
		m_player->player->GetComponent<CharacterControllerComponent>()->EndKnockBack(); //&&&&&  넉백이랑같이  쓸함수 이름수정할거
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
	}
}
