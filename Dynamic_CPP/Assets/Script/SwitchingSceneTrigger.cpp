#include "SwitchingSceneTrigger.h"
#include "TextComponent.h"
#include "ImageComponent.h"
#include "InputManager.h"
#include "GameInstance.h"
#include "pch.h"

void SwitchingSceneTrigger::Start()
{
	m_buttonText = GetOwner()->GetComponent<TextComponent>();
	if (m_buttonText)
	{
		m_buttonText->SetAlpha(0);
	}
	int childIndex = GetOwner()->m_childrenIndices[0];
	GameObject* child = GameObject::FindIndex(childIndex);
	if (child)
	{
		m_switchingText = child->GetComponent<TextComponent>();
		if (m_switchingText)
		{
			m_switchingText->SetAlpha(0);
		}
	}
}

void SwitchingSceneTrigger::Update(float tick)
{
	if (!m_buttonText || !m_switchingText) return;

	if (/*!GameInstance::GetInstance()->IsLoadSceneComplete()*/true)
	{
		if (!m_isFadeInComplete)
		{
			m_fadeTimer += tick;
			float alpha = std::clamp(m_fadeTimer / m_fadeDuration, 0.0f, 1.0f);
			m_buttonText->SetAlpha(alpha);
			m_switchingText->SetAlpha(alpha);
			if (alpha >= 1.0f)
			{
				m_isFadeInComplete = true;
				m_fadeTimer = 0.0f; // Reset timer for potential future use
			}
		}
	}

	if (m_isFadeInComplete)
	{
		//여기서 씬 전환
		if (InputManagement->IsControllerButtonDown(0, ControllerButton::A))
		{
		}
	}
}

