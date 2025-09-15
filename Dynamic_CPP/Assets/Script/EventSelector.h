#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class EventSelector : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(EventSelector)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
	int m_currentID{ -1 };
};
