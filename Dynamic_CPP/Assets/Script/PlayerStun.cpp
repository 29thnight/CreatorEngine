#include "PlayerStun.h"
#include "pch.h"
#include "Player.h"
#include "AnimationController.h"
#include "Animator.h"
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
}

void PlayerStun::Update(float deltaTime)
{

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

void PlayerStun::Exit()
{
}
