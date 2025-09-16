#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"
#include "EventTarget.generated.h"

// Legacy action target (kept for compatibility with existing game code)
class EventTarget : public ModuleBehavior
{
public:
   ReflectEventTarget
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EventTarget)
	// New purpose: bind a runtime variable into EventManager so objectives can be late-bound
	// Defaults to varName="targetTag"
	void SetManager(class EventManager* mgr) { m_mgr = mgr; }
	void Configure(int eventId, const std::string& varName, const std::string& value)
	{
		m_eventId = eventId; m_varName = varName; m_value = value;
		if (m_mgr) m_mgr->SetEventVar(m_eventId, m_varName, m_value);
	}

	void Awake() override
	{
		Apply();
	}

	[[Method]]
	void Apply()
	{
		if (!m_mgr)
		{
			auto mgr = GameInstance::GetInstance()->GetActiveEventManager();
			if (mgr) SetManager(mgr);
		}

		if (m_mgr && m_eventId != 0 && !m_value.empty())
			m_mgr->SetEventVar(m_eventId, m_varName, m_value);
	}

private:
	[[Property]]
	int m_eventId{ 0 };
	[[Property]]
	std::string m_varName{ "targetTag" };
	[[Property]]
	std::string m_value{};
	class EventManager* m_mgr{ nullptr };
};
