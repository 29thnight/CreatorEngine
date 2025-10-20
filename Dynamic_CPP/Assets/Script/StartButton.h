#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"

class StartButton : public ImageButton
{
public:
	MODULE_BEHAVIOR_BODY(StartButton)
	virtual void Start() override;
	virtual void Update(float tick) override;

public:
	virtual void ClickFunction() override;

private:
	using Super = ImageButton;
};
