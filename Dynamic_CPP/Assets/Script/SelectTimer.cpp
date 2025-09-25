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

	//TODO : 타이머 텍스트 컴포넌트 초기화 단계에서 받아서 m_remainTimeInternal 출력 할 것
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
		//TODO: 텍스트 컴포넌트 비활성화 시킬것
	}
}

