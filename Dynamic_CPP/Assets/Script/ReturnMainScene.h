#pragma once
#include "Core.Minimal.h"
#include "ImageButton.h"

class ReturnMainScene : public ImageButton
{
public:
	MODULE_BEHAVIOR_BODY(ReturnMainScene)
	virtual void Start() override;
	virtual void Update(float tick) override;

	virtual void ClickFunction() override;

private:
	using Super = ImageButton;

	class GameManager* m_gameManager{};
	bool m_isClicked{ false };
};
