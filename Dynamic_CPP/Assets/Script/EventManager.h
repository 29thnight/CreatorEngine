#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventDef.h"

class EventTarget; // legacy compat
class ITriggerCondition; // trigger interface

struct EventRuntime
{
	EventDefinition def;
	EventStatus status{ EventStatus::NotStarted };
	// Runtime variables (late-bound from world objects or scripts)
	// e.g., { "targetTag": "Resource_A", "actorTag": "Asys" }
	std::unordered_map<std::string, std::string> vars;
	// Objective states in one-to-one order with def.objectives
	std::vector<ObjectiveState> objectives;
};

class EventManager : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(EventManager)
	void Awake() override;
	void Update(float tick) override;
	void OnDestroy() override;

	// --- Runtime control ---
	void StartEvent(int id);
	void CompleteEvent(int id);
	void FailEvent(int id);
	void AdvanceFrom(int id);

	// Signals coming from gameplay or triggers
	void PushSignal(int eventId, const EventSignal& sig);

	// Registration APIs
	void RegisterTrigger(int id, ITriggerCondition* trig);

	// Lookup
	EventDefinition* GetEventDefinition(int id);

	// Optional: current players (for PlayerScope evaluation)
	const std::vector<int>& GetActivePlayers() const { return m_activePlayers; }
	void SetActivePlayers(std::vector<int> ids) { m_activePlayers = std::move(ids); }

	// --- Runtime variables (late binding) ---
	void SetEventVar(int id, const std::string& key, const std::string& value);
	const EventDefinition* GetRuntimeDef(int id) const;

	std::vector<int> GetActiveEventIds() const;

	// >>> ADD: 시그널 브로드캐스트 & 편의 래퍼들
	void BroadcastSignal(const EventSignal& sig);

	void EmitObjectDestroyed(const std::string& tag, int playerId = -1);
	void EmitEnemyKilled(const std::string& groupOrTag, int playerId = -1);
	void EmitInteracted(const std::string& actorTag, const std::string& withTag, int playerId = -1);
	void EmitAbilityUsed(const std::string& ability, const std::string& contextTag = std::string{}, int playerId = -1);
	void EmitReachedTrigger(const std::string& actorTag, int triggerIndex, int playerId = -1);
	void EmitPurchased(const std::string& itemTag, int count = 1, int playerId = -1);
	void EmitDebuffApplied(int playerId, const std::string& debuffTag);
	void EmitDebuffRemoved(int playerId, const std::string& debuffTag);
	void EmitDelivered(const std::string& itemTag, int count = 1, int playerId = -1);

private:
	// Loading & mapping
	void LoadDefinitions();

	// Resolve ${var} or $var in objective fields from runtime vars
	void ResolveVariables(int id);
	static EventCategory ParseCategory(const std::string& s);
	static PlayerScope ParsePlayerScope(const std::string& s);
	static AdvancePolicy ParseAdvancePolicy(std::string& s);
	static ObjectiveType ParseObjectiveType(const std::string& s);

	// Parse objectives from a single CSV column: "Objectives"
	// DSL example: Destroy tag=Resource required=3; ReachTrigger actor=Asys index=1
	bool ParseObjectivesDSL(const std::string& dsl, EventDefinition& out);

	void EnsureRuntime(int id);
	bool EvaluateAndMaybeComplete(int id);

private:
	std::vector<EventDefinition> m_definitions;
	std::unordered_map<int, size_t> m_indexById; // id->index in m_definitions

	std::unordered_map<int, EventRuntime> m_runtime; // active or visited runtime data
	std::unordered_map<int, ITriggerCondition*> m_triggers; // bound triggers per event

	std::vector<int> m_activePlayers{ 0, 1 }; // default two players (0,1)

};
