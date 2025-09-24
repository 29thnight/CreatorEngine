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
		m_uiButton->SetClickFunction([&]() { ClickFunction(); });
	}
}

void ImageButton::ClickFunction()
{
	//테스트 기본
	GameInstance::GetInstance()->SwitchScene("CharSelectScene");
}

