#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"

class SettingButton : public ImageButton
{
public:
	MODULE_BEHAVIOR_BODY(SettingButton)
	virtual void Start() override;
	virtual void Update(float tick) override;
public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
	class GameObject* m_settingCanvasObj{ nullptr };
	class GameObject* m_settingWindowObj{ nullptr };
};
