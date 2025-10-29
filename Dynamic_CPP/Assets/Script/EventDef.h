#pragma once
#include <string>
#include <vector>
#include <unordered_map>


// High-level step types (Excel Category)
enum class EventCategory : uint8_t
{
	Non = 0, // system / transitions
	UI, // guidance text only
	Quest, // real objectives
	Trigger // trigger-only step (reach point, etc.)
};


// Player participation scope (Excel Player: O/X/non)
enum class PlayerScope : uint8_t
{
	None = 0, // no player involvement
	AllPlayersIndividually, // O : each player must meet condition
	AnyPlayer, // X : any one player can satisfy
	Shared // shared/pooled counter among players
};


// When to auto-advance to the next step
enum class AdvancePolicy : uint8_t
{
	AutoAdvance = 0, // immediately start next when completed
	WaitForNextTrigger,// wait for an external trigger/confirm
	Manual // advance by explicit API call
};

// What kind of objective this is
enum class ObjectiveType : uint8_t
{
	Destroy = 0, // destroy target objects (by tag)
	Interact, // interact with target (actor vs withTag)
	EliminateAll, // remaining = 0 for a group
	KillCount, // kill X enemies (by tag/group)
	CollectCount, // collect items
	Deliver, // deliver items to target
	SurviveNoDebuff, // survive time without specific debuff
	UseAbilityToDestroy, // use ability to destroy something
	ReachTrigger, // reach trigger index/location
	Purchase, // shop purchase count
	Timer, // wait duration
	ChangeScene, // scene transition
	Custom // extension hook
};

struct ObjectiveDef
{
	ObjectiveType type{ ObjectiveType::Destroy };
	std::string targetTag; // e.g. "Resource", "Wave_1"
	std::string targetActor; // e.g. "Asys"
	int requiredCount{ 1 };
	float durationSeconds{ 0.f };
	std::string ability; // e.g. "Charge"
	int triggerIndex{ -1 }; // e.g. Nth waypoint/trigger
	// Free-form parameters (string-based) for modding/future-proof
	std::unordered_map<std::string, std::string> params;
};

struct EventDefinition
{
	int id{ 0 };
	std::string name; // QuestName / EventTitle
	std::string scene; // Scene (optional)
	EventCategory category{ EventCategory::Quest };
	PlayerScope playerScope{ PlayerScope::Shared };
	int priorId{ 0 };
	int nextId{ 0 };
	AdvancePolicy advance{ AdvancePolicy::AutoAdvance };
	// UI text / description shown for Category::UI
	std::string uiText;
	// Objectives (0..N); UI/Non may be empty and act as pure action/trigger steps
	std::vector<ObjectiveDef> objectives;
	// Arbitrary parameters for actions/FX/etc.
	std::unordered_map<std::string, std::string> parameters;
};

// Optional runtime status per-event
enum class EventStatus : uint8_t { NotStarted, Active, Completed, Failed };

// Signals raised by gameplay systems and triggers -> consumed by EventManager
enum class EventSignalType : uint8_t
{
	ObjectDestroyed = 0, // tag, playerId?
	EnemyKilled, // tag/group, playerId?
	Interacted, // actorTag, withTag, playerId
	AbilityUsed, // ability, contextTag, playerId
	ReachedTrigger, // actorTag, triggerIndex
	Purchased, // itemTag, count, playerId
	DebuffApplied,
	DebuffRemoved,
	TickTimer, // objective-local or global tick
};

struct EventSignal
{
	EventSignalType type{};
	int playerId{ -1 }; // -1 when not applicable
	// Generic payload (simple and flexible)
	std::string a; // tag/actor/ability/item
	std::string b; // withTag/context/groupId
	int i{ 0 }; // count/triggerIndex
	float f{ 0.f }; // duration/amount
};

struct ObjectiveState
{
	int progressShared{ 0 }; // shared counter
	std::unordered_map<int, int> perPlayer; // playerId -> progress
	float timerAccum{ 0.f }; // for Timer/Survive objectives
	bool completed{ false };
};

class ObjectiveEvaluator
{
public:
	static bool OnSignal(const ObjectiveDef& def, ObjectiveState& st, const EventSignal& sig)
	{
		switch (def.type)
		{
		case ObjectiveType::Deliver:
			if (sig.type == EventSignalType::ObjectDestroyed && (def.targetTag.empty() || sig.a == def.targetTag))
			{
				// add sig.i to count; default i==1
				return Bump(st, sig.playerId, def, sig.i > 0 ? sig.i : 1);
			}
			break;
		case ObjectiveType::Destroy:
			if (sig.type == EventSignalType::ObjectDestroyed && (def.targetTag.empty() || sig.a == def.targetTag))
				return Bump(st, sig.playerId, def);
			break;
		case ObjectiveType::KillCount:
			if (sig.type == EventSignalType::EnemyKilled && (def.targetTag.empty() || sig.b == def.targetTag))
			{
				// add sig.i to count; default i==1
				return Bump(st, sig.playerId, def, sig.i > 0 ? sig.i : 1);
			}
			break;
		case ObjectiveType::EliminateAll:
			// Typically driven elsewhere: broadcast remaining count via sig.i
			if (sig.type == EventSignalType::EnemyKilled && (def.targetTag.empty() || sig.b == def.targetTag))
				return Bump(st, sig.playerId, def);
			break;
		case ObjectiveType::Interact:
			if (sig.type == EventSignalType::Interacted)
			{
				const bool okActor = def.targetActor.empty() || sig.a == def.targetActor;
				const bool okWith = def.targetTag.empty() || sig.b == def.targetTag;
				if (okActor && okWith) return Bump(st, sig.playerId, def);
			}
			break;
		case ObjectiveType::UseAbilityToDestroy:
			if (sig.type == EventSignalType::AbilityUsed && (def.ability.empty() || sig.a == def.ability))
				return Bump(st, sig.playerId, def);
			break;
		case ObjectiveType::ReachTrigger:
			if (sig.type == EventSignalType::ReachedTrigger)
			{
				const bool okActor = def.targetActor.empty() || sig.a == def.targetActor;
				const bool okIdx = (def.triggerIndex < 0) || (sig.i == def.triggerIndex);
				if (okActor && okIdx) return Bump(st, sig.playerId, def);
			}
			break;
		case ObjectiveType::Purchase:
			if (sig.type == EventSignalType::Purchased)
			{
				// add sig.i to count; default i==1
				return Bump(st, sig.playerId, def, sig.i > 0 ? sig.i : 1);
			}
			break;
		case ObjectiveType::Timer:
			if (sig.type == EventSignalType::TickTimer)
			{
				st.timerAccum += sig.f; // f = deltaSeconds
				if (st.timerAccum >= def.durationSeconds)
				{
					st.completed = true;
					return true;
				}
			}
			break;
		case ObjectiveType::SurviveNoDebuff:
			if (sig.type == EventSignalType::TickTimer)
			{
				// For a simple version: just accumulate time (assume caller filters debuff state)
				st.timerAccum += sig.f;
				if (st.timerAccum >= def.durationSeconds) { st.completed = true; return true; }
			}
			break;
		default: break;
		}
		return false;
	}


	static bool IsCompleted(const ObjectiveDef& def, const ObjectiveState& st, PlayerScope scope, const std::vector<int>& playerIds)
	{
		if (st.completed) return true;
		switch (scope)
		{
		case PlayerScope::None:
			return st.progressShared >= def.requiredCount;
		case PlayerScope::Shared:
			return st.progressShared >= def.requiredCount;
		case PlayerScope::AnyPlayer:
			for (int pid : playerIds)
				if (GetProgress(st, pid) >= def.requiredCount) return true;
			return false;
		case PlayerScope::AllPlayersIndividually:
			for (int pid : playerIds)
				if (GetProgress(st, pid) < def.requiredCount) return false;
			return true;
		}
		return false;
	}

private:
	static int GetProgress(const ObjectiveState& st, int playerId)
	{
		auto it = st.perPlayer.find(playerId);
		return (it == st.perPlayer.end()) ? 0 : it->second;
	}


	static bool Bump(ObjectiveState& st, int playerId, const ObjectiveDef& def, int delta = 1)
	{
		if (playerId >= 0) st.perPlayer[playerId] += delta;
		st.progressShared += delta;
		// completion check done by caller via IsCompleted()
		return true;
	}
};