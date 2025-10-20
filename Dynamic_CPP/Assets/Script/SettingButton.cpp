#include "SettingButton.h"
#include "Canvas.h"
#include "SettingWindowUI.h"
#include "pch.h"
void SettingButton::Start()
{
	Super::Start();

	m_settingCanvasObj = GameObject::Find("SettingCanvas");
	m_settingCanvasObj->SetEnabled(false);
	m_settingWindowObj = GameObject::Find("SettingWindow");
}

void SettingButton::Update(float tick)
{
	Super::Update(tick);
}

void SettingButton::ClickFunction()
{
	if (m_settingCanvasObj && m_settingWindowObj)
	{
		auto settingUI = m_settingWindowObj->GetComponent<SettingWindowUI>();
		if (settingUI)
		{
			settingUI->SetEntering(true);
			m_settingCanvasObj->SetEnabled(true);
		}
	}
}

