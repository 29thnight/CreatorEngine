#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"

class EventTarget : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(EventTarget)

	virtual void Execute(const EventDefinition& def) = 0;
};