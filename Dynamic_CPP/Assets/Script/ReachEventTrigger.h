#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ReachEventTrigger.generated.h"

class ReachEventTrigger : public ModuleBehavior
{
public:
   ReflectReachEventTrigger
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ReachEventTrigger)
	virtual void Awake() override;
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;

public:
	[[Property]]
	int m_triggerIndex{ 0 };
	[[Property]]
	bool m_emitOnEnter{ true };
	[[Property]]
	bool m_emitOnStay{ false };
	[[Property]]
	bool m_emitOnExit{ false };
	// 한 번만 보낼지(Enter 1회 등)
	[[Property]] 
	bool m_once{ false };
	// 플레이어/주체 식별 (필요 시 사용)
	[[Property]] 
	int m_playerId{ -1 };
	// 모두 통과해야하는지
	[[Property]]
	bool m_allPlayerPass{ false };

private:
	bool EmitIfAllowed(const Collision& c);
	std::string ResolveActorTag(const Collision& c) const;
	void EnsureManager();

	bool ShouldEmitOnEnter() const { return m_emitOnEnter; }
	bool ShouldEmitOnStay() const { return m_emitOnStay; }
	bool ShouldEmitOnExit() const { return m_emitOnExit; }

	std::unordered_set<int> onTriggeredPlayer{};
	class EventManager* m_mgr{ nullptr };
	bool m_alreadyEmitted{ false };
};
