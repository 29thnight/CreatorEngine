#include "SelectTimer.h"
#include "GameObject.h"
#include "GameManager.h"
#include "pch.h"
void SelectTimer::Start()
{
	auto gameManagerObj = GameObject::Find("GameManager");
	if (gameManagerObj)
	{
		gameManager = gameManagerObj->GetComponent<GameManager>();
	}

	//TODO : Ÿ�̸� �ؽ�Ʈ ������Ʈ �ʱ�ȭ �ܰ迡�� �޾Ƽ� m_remainTimeInternal ��� �� ��
}

void SelectTimer::Update(float tick)
{
	if (!gameManager) return;
	int count = gameManager->selectPlayerCount;
	
	if (2 <= count)
	{
		if(0 >= m_remainTimeInternal)
		{
			m_remainTimeInternal = m_remainTimeSetting;
		}

		m_remainTimeInternal -= tick;

	}
	else
	{
		m_remainTimeInternal = -1.f;
		//TODO: �ؽ�Ʈ ������Ʈ ��Ȱ��ȭ ��ų��
	}
}

