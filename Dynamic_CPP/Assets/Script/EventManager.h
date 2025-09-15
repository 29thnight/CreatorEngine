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

	EventDefinition* GetEventDefinition(int id)
	{
		auto defIt = std::find_if(m_definitions.begin(), m_definitions.end(),
			[id](const EventDefinition& def) { return def.id == id; });
		if (defIt == m_definitions.end())
		{
			std::cerr << "EventManager::GetEventDefinition - definition not found for id " << id << '\n';
			return nullptr;
		}
		return &(*defIt);
	}

private:
	std::vector<EventDefinition> m_definitions{};
	std::unordered_map<int, class EventTarget*> m_targets{};
};
