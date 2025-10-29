#include "PauseMenuButton.h"
#include "Canvas.h"
#include "UIManager.h"
#include "ImageComponent.h"
#include "InputManager.h"
#include "RectTransformComponent.h"
#include "GameInstance.h"
#include "pch.h"

void PauseMenuButton::Start()
{
	m_boxImageComponent = GetOwner()->GetComponent<ImageComponent>();
	int parentIndex = GetOwner()->m_parentIndex;
	if (parentIndex != -1)
	{
		m_pauseMenuCanvasObject = GameObject::FindIndex(parentIndex);
	}
}

void PauseMenuButton::OnEnable()
{
	m_isMenuOpened = true;
	GameInstance::GetInstance()->PauseGame();
}

void PauseMenuButton::Update(float tick)
{
	if (m_isMenuOpened)
	{
		if (m_pauseMenuCanvasObject)
		{
			UIManagers->SelectUI = GetOwner()->weak_from_this();
		}
		m_isMenuOpened = false;
	}

	// �ݱ�(B / X)
	bool wantClose = false;
	if (InputManagement->IsKeyDown(KeyBoard::X) ||
		InputManagement->IsControllerButtonDown(0, ControllerButton::B) ||
		InputManagement->IsControllerButtonDown(1, ControllerButton::B))
	{
		wantClose = true;
	}
	if (wantClose)
	{
		if (m_pauseMenuCanvasObject) m_pauseMenuCanvasObject->SetEnabled(false);
		return;
	}

}

void PauseMenuButton::OnDisable()
{
	// ������ �⺻������ ����
	m_active = PauseWindowType::Self;
	m_isTransitioning = false;
	// �ʿ��ϸ� �׺� ��� ����
	if (m_boxImageComponent && m_boxImageComponent->IsNavLock())
		m_boxImageComponent->SetNavLock(false);

	GameInstance::GetInstance()->ResumeGame();
}

