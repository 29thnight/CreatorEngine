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
		m_player->SetInvincibility(m_player->dashGracePeriod);
		m_player->DropCatchItem();
		auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		controller->Move({ 0 ,0 });



		float knockbackSpeed = m_player->dashDistacne / m_player->m_dashTime;
		m_player->curDashTime = m_player->m_dashTime;
		Mathf::Vector3 forward = m_player->player->m_transform.GetForward();
		Mathf::Vector3 horizontalDir = forward;
		horizontalDir.y = 0.0f;
		horizontalDir.Normalize();

		// 속도 벡터 계산
		Mathf::Vector3 knockbackVelocity = horizontalDir * knockbackSpeed;

		controller->TriggerForcedMove(knockbackVelocity, m_player->m_dashTime);

		m_player->isDashing = true;
		m_player->m_dashElapsedTime = 0.f;

		//m_player->m_animator->SetUseLayer(1, false);
		if (m_player->dashEffect)
			m_player->dashEffect->Apply();
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
		//m_player->player->GetComponent<CharacterControllerComponent>()->StopForcedMove(); //&&&&&  넉백이랑같이  쓸함수 이름수정할거
		//auto controller = m_player->player->GetComponent<CharacterControllerComponent>();
		//controller->Move({ 0 ,0 });
	}

}
