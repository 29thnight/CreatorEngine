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
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnDestroy() override;

	[[Method]]
	void Apply();

	int playerID{ -1 };

private:
	[[Property]]
	int m_eventId{ 0 };
	std::string m_targetTag{ "targetTag" };
	[[Property]]
	std::string m_runtimeTag{};
	EventManager* m_mgr{ nullptr };
};
