#include "PlayerGrab.h"
#include "pch.h"
#include "Player.h"
#include "Animator.h"
void PlayerGrab::Enter()
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
		m_player->m_animator->SetUseLayer(1, true);

	}
}

void PlayerGrab::Update(float deltaTime)
{
}

void PlayerGrab::Exit()
{
	if (m_player)
	{
		m_player->m_animator->SetUseLayer(1, false);

	}
}
