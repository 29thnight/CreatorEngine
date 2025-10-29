#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "StartButton.generated.h"

class StartButton : public ImageButton
{
public:
   ReflectStartButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(StartButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;

	class GameManager* m_gameManager{ nullptr };
	[[Property]]
	bool m_isClicked{ false };
};
