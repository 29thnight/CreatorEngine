#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "RestartButton.generated.h"

class RestartButton : public ImageButton
{
public:
   ReflectRestartButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(RestartButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

	virtual void ClickFunction() override;

private:
	using Super = ImageButton;

	class GameManager* m_gameManager{};
	bool m_isClicked{ false };

	[[Property]]
	bool m_isEntering{ false };
};
