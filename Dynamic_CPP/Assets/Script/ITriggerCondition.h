#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"

class ITriggerCondition : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(ITriggerCondition)

	void Bind(int eventID, class EventManager* mgr) { 
		m_eventID = eventID; 
		m_eventManager = mgr; 
	}

	void Raise(const EventSignal& sig);

protected:
	int m_eventID{};
	class EventManager* m_eventManager{};
};