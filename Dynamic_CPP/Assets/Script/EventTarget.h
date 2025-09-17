#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"
#include "EventTarget.generated.h"

// Legacy action target (kept for compatibility with existing game code)
class EventManager;
class EventTarget : public ModuleBehavior
{
public:
   ReflectEventTarget
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EventTarget)
	// New purpose: bind a runtime variable into EventManager so objectives can be late-bound
	// Defaults to varName="targetTag"
	void SetManager(EventManager* mgr) { m_mgr = mgr; }
   void Configure(int eventId, const std::string& varName, const std::string& value);

	virtual void Awake() override;

	[[Method]]
	void Apply();

private:
	[[Property]]
	int m_eventId{ 0 };
	[[Property]]
	std::string m_varName{ "targetTag" };
	[[Property]]
	std::string m_value{};
	EventManager* m_mgr{ nullptr };
};
