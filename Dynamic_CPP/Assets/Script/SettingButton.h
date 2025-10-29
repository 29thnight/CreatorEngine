#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "SettingButton.generated.h"

class SettingButton : public ImageButton
{
public:
   ReflectSettingButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(SettingButton)
	virtual void Start() override;
	virtual void Update(float tick) override;
public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
	class GameObject* m_settingCanvasObj{ nullptr };
	class GameObject* m_settingWindowObj{ nullptr };
	[[Property]]
	bool m_isEntering{ false };
};
