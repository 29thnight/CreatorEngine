#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"
#include "TutorialButton.generated.h"

class TutorialButton : public ImageButton
{
public:
   ReflectTutorialButton
	[[ScriptReflectionField(Inheritance:ImageButton)]]
	MODULE_BEHAVIOR_BODY(TutorialButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;

	[[Property]]
	bool m_isEntering{ false };
};
