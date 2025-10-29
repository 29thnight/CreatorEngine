#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "CreditsButton.generated.h"

class CreditsButton : public ImageButton
{
public:
   ReflectCreditsButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(CreditsButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;

	bool m_isClicked = false;
	class GameManager* m_gameManager{};
	[[Property]]
	bool m_isEntering{ false };
};
