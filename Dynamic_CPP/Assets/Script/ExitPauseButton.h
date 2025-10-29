#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "ExitPauseButton.generated.h"

class ExitPauseButton : public ImageButton
{
public:
   ReflectExitPauseButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(ExitPauseButton)
	virtual void Start() override;
	virtual void Update(float tick) override;
	
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
	class GameObject* m_pauseMenuCanvasObject{};

	bool m_isClicked{ false };
	[[Property]]
	bool m_isEntering{ false };
};
