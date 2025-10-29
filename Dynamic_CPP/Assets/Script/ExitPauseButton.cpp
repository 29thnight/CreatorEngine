#include "ExitPauseButton.h"
#include "GameInstance.h"
#include "pch.h"

void ExitPauseButton::Start()
{
	Super::Start();

	int parentIndex = GetOwner()->m_parentIndex;
	if (parentIndex != -1)
	{
		auto parentObj = GameObject::FindIndex(parentIndex);
		if (parentObj)
		{
			int grandParentIndex = parentObj->m_parentIndex;
			if (grandParentIndex != -1)
			{
				m_pauseMenuCanvasObject = GameObject::FindIndex(grandParentIndex);
			}
		}
	}
}

void ExitPauseButton::Update(float tick)
{
	Super::Update(tick);
}

void ExitPauseButton::ClickFunction()
{
	if (m_pauseMenuCanvasObject) m_pauseMenuCanvasObject->SetEnabled(false);
	GameInstance::GetInstance()->ResumeGame();
}

