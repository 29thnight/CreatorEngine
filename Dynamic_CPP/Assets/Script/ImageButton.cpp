#include "ImageButton.h"
#include "ImageComponent.h"
#include "GameInstance.h"
#include "UIButton.h"
#include "pch.h"

void ImageButton::Start()
{
	m_imageComponent = GetComponent<ImageComponent>();
	//GameInstance::GetInstance()->LoadScene("LoadingScene");
}

void ImageButton::Update(float tick)
{
	if (!m_uiButton)
	{
		m_uiButton = GetComponent<UIButton>();
		if (m_uiButton)
		{
			m_uiButton->SetClickFunction([&]() { ClickFunction(); });
		}
	}

	if (m_imageComponent)
	{
		if(m_imageComponent->IsNavigationThis())
		{
			m_imageComponent->SetEnabled(true);
		}
		else
		{
			m_imageComponent->SetEnabled(false);
		}
	}
}

void ImageButton::ClickFunction()
{
	//테스트 기본
	GameInstance::GetInstance()->SwitchScene("CharSelectScene");
}

