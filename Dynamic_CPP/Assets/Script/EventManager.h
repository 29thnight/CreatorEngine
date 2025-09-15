#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"

class EventManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(EventManager)
	virtual void Awake() override;

	void Notify(int id);

	void RegisterTarget(int id, class EventTarget* target);

private:
	std::vector<EventDefinition> m_definitions{};
	std::unordered_map<int, class EventTarget*> m_targets{};
};
