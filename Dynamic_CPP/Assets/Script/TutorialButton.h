#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"

class TutorialButton : public ImageButton
{
public:
	MODULE_BEHAVIOR_BODY(TutorialButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
};
