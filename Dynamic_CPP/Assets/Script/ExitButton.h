#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"

class ExitButton : public ImageButton
{
public:
	MODULE_BEHAVIOR_BODY(ExitButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
};
