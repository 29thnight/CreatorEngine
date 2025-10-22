#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class MenuKeyObserver : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MenuKeyObserver)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	bool m_isMenuOpened{ false };
	class GameManager* m_gameManager{ nullptr };
};
