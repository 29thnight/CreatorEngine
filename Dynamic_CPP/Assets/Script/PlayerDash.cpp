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
		AnimationController* controller = m_ownerController;
		if (controller)
		{
			Animator* Playerani = controller->GetOwner();
			if (Playerani)
			{
				GameObject* player = Playerani->GetOwner();
				if (player)
				{
					GameObject* parent = GameObject::FindIndex(player->m_parentIndex);
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
		m_player->GetOwner()->SetLayer("PlayerDash");
		m_player->ChangeState("Dash");
		m_player->SetInvincibility(4.f);
		m_player->DropCatchItem();
		//m_player->m_animator->SetUseLayer(1, false);
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
		m_player->GetOwner()->SetLayer("Player");
		m_player->ChangeState("Idle");
		m_player->EndInvincibility();
		//m_player->m_animator->SetUseLayer(1, true);
		if (m_player->dashEffect)
			m_player->dashEffect->StopEffect();
		m_player->player->GetComponent<CharacterControllerComponent>()->StopForcedMove(); //&&&&&  �˹��̶�����  ���Լ� �̸������Ұ�
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });
	}

}
