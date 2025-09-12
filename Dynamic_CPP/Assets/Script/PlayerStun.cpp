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

void PlayerStun::Exit()
{
}
