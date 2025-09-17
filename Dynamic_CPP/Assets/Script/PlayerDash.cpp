#include "PlayerDash.h"
#include "pch.h"
#include "Player.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "EffectComponent.h"
void PlayerDash::Enter()
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
		m_player->ChangeState("Dash");
		m_player->DropCatchItem();
		m_player->m_animator->SetUseLayer(1, false);
		if (m_player->dashEffect)
			m_player->dashEffect->Apply();
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({0 ,0 });
	}
}

void PlayerDash::Update(float deltaTime)
{
}

void PlayerDash::Exit()
{
	if (m_player)
	{
		m_player->ChangeState("Idle");
		m_player->m_animator->SetUseLayer(1, true);
		if (m_player->dashEffect)
			m_player->dashEffect->StopEffect();
		m_player->player->GetComponent<CharacterControllerComponent>()->EndKnockBack(); //&&&&&  넉백이랑같이  쓸함수 이름수정할거
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
	}

}
