#include "EventManager.h"
#include "EventTarget.h"
#include "CSVLoader.h"
#include "GameInstance.h"
#include "ITriggerCondition.h"
#include "StringHelper.h"
#include "pch.h"

static bool HasColumn(const CSVReader::Row& row, const char* name)
{
    try { (void)row[name]; return true; }
    catch (...) { return false; }
}

void EventManager::Awake()
{
	GameInstance::GetInstance()->SetActiveEventManager(this);

    LoadDefinitions();

    // Optionally auto-start the first event that has priorId==0
    for (auto& def : m_definitions)
    {
        if (def.priorId == 0)
        {
            StartEvent(def.id);
            break;
        }
    }
}

void EventManager::Update(float tick)
{
    if (m_runtime.empty()) return;

    EventSignal tickSignal{ EventSignalType::TickTimer };
    tickSignal.f = tick;

    for (auto& kv : m_runtime)
    {
        const int id = kv.first;
        auto& rt = kv.second;
        if (rt.status != EventStatus::Active) continue;

        bool touched = false;
        for (size_t i = 0; i < rt.def.objectives.size(); ++i)
        {
            const auto& o = rt.def.objectives[i];
            if (o.type == ObjectiveType::Timer)
            {
                if (ObjectiveEvaluator::OnSignal(o, rt.objectives[i], tickSignal))
                    touched = true;
            }
        }
        if (touched) EvaluateAndMaybeComplete(id);
    }

}

void EventManager::OnDestroy()
{
	GameInstance::GetInstance()->ClearActiveEventManager();
}

void EventManager::StartEvent(int id)
{
    EnsureRuntime(id);
    auto& rt = m_runtime[id];
    if (rt.status == EventStatus::Active) return;
    rt.status = EventStatus::Active;

    // Bind trigger if any registered for this event
    auto itT = m_triggers.find(id);
    if (itT != m_triggers.end() && itT->second)
        itT->second->Bind(id, this);

    // If this is a pure UI step and auto-advance, we can complete immediately or on confirm
    if (rt.def.category == EventCategory::UI && rt.def.objectives.empty())
    {
        if (rt.def.advance == AdvancePolicy::AutoAdvance)
            CompleteEvent(id);
        else if (rt.def.advance == AdvancePolicy::WaitForNextTrigger)
            ; // wait for external confirm
    }
}

void EventManager::CompleteEvent(int id)
{
    EnsureRuntime(id);
    auto& rt = m_runtime[id];
    rt.status = EventStatus::Completed;

    // Auto advance if configured
    if (rt.def.advance == AdvancePolicy::AutoAdvance && rt.def.nextId != 0)
        StartEvent(rt.def.nextId);
}

void EventManager::FailEvent(int id)
{
    EnsureRuntime(id);
    m_runtime[id].status = EventStatus::Failed;
}

void EventManager::AdvanceFrom(int id)
{
    EnsureRuntime(id);
    auto& rt = m_runtime[id];
    if (rt.def.nextId != 0) StartEvent(rt.def.nextId);
}

void EventManager::PushSignal(int eventId, const EventSignal& sig)
{
    EnsureRuntime(eventId);
    auto& rt = m_runtime[eventId];
    if (rt.status != EventStatus::Active) return;


    bool anyChanged = false;
    for (size_t i = 0; i < rt.def.objectives.size(); ++i)
    {
        if (ObjectiveEvaluator::OnSignal(rt.def.objectives[i], rt.objectives[i], sig))
            anyChanged = true;
    }
    if (anyChanged) EvaluateAndMaybeComplete(eventId);
}

void EventManager::RegisterTrigger(int id, ITriggerCondition* trig)
{
    if (!trig) return;
    m_triggers[id] = trig;
}

EventDefinition* EventManager::GetEventDefinition(int id)
{
    auto it = m_indexById.find(id);
    if (it == m_indexById.end()) return nullptr;
    return &m_definitions[it->second];
}

void EventManager::SetEventVar(int id, const std::string& key, const std::string& value)
{
    EnsureRuntime(id);
    auto& rt = m_runtime[id];
    rt.vars[key] = value;
    // Re-apply if already active or about to start
    ResolveVariables(id);
}

const EventDefinition* EventManager::GetRuntimeDef(int id) const
{
    auto it = m_runtime.find(id);
    if (it == m_runtime.end()) return nullptr;
    return &it->second.def;
}

std::vector<int> EventManager::GetActiveEventIds() const
{
    std::vector<int> out;
    out.reserve(m_runtime.size());
    for (const auto& kv : m_runtime)
    {
        if (kv.second.status == EventStatus::Active)
            out.push_back(kv.first);
    }
    // (선택) 일정한 출력 순서를 원하면 정렬
    std::sort(out.begin(), out.end());
    return out;
}

void EventManager::BroadcastSignal(const EventSignal& sig)
{
    for (auto& kv : m_runtime)
    {
        const int id = kv.first;
        auto& rt = kv.second;
        if (rt.status != EventStatus::Active) continue;

        bool anyChanged = false;
        for (size_t i = 0; i < rt.def.objectives.size(); ++i)
        {
            if (ObjectiveEvaluator::OnSignal(rt.def.objectives[i], rt.objectives[i], sig))
                anyChanged = true;
        }
        if (anyChanged) EvaluateAndMaybeComplete(id);
    }
}

void EventManager::EmitObjectDestroyed(const std::string& tag, int playerId)
{
    EventSignal s{ EventSignalType::ObjectDestroyed };
    s.playerId = playerId;
    s.a = tag;
    BroadcastSignal(s);
}

void EventManager::EmitEnemyKilled(const std::string& groupOrTag, int playerId)
{
    EventSignal s{ EventSignalType::EnemyKilled };
    s.playerId = playerId;
    s.b = groupOrTag;          // EliminateAll에서 sig.b 사용
    BroadcastSignal(s);
}

void EventManager::EmitInteracted(const std::string& actorTag, const std::string& withTag, int playerId)
{
    EventSignal s{ EventSignalType::Interacted };
    s.playerId = playerId;
    s.a = actorTag;
    s.b = withTag;
    BroadcastSignal(s);
}

void EventManager::EmitAbilityUsed(const std::string& ability, const std::string& contextTag, int playerId)
{
    EventSignal s{ EventSignalType::AbilityUsed };
    s.playerId = playerId;
    s.a = ability;
    s.b = contextTag;
    BroadcastSignal(s);
}

void EventManager::EmitReachedTrigger(const std::string& actorTag, int triggerIndex)
{
    EventSignal s{ EventSignalType::ReachedTrigger };
    s.a = actorTag;
    s.i = triggerIndex;
    BroadcastSignal(s);
}

void EventManager::EmitPurchased(const std::string& itemTag, int count, int playerId)
{
    EventSignal s{ EventSignalType::Purchased };
    s.playerId = playerId;
    s.a = itemTag;
    s.i = count;
    BroadcastSignal(s);
}

void EventManager::EmitDebuffApplied(int playerId, const std::string& debuffTag)
{
    EventSignal s{ EventSignalType::DebuffApplied };
    s.playerId = playerId;
    s.a = debuffTag;
    BroadcastSignal(s);
}

void EventManager::EmitDebuffRemoved(int playerId, const std::string& debuffTag)
{
    EventSignal s{ EventSignalType::DebuffRemoved };
    s.playerId = playerId;
    s.a = debuffTag;
    BroadcastSignal(s);
}

void EventManager::LoadDefinitions()
{
    m_definitions.clear();
    m_indexById.clear();

    // Try new tutorial csv first
    bool loaded = false;
    try
    {
		//TODO : 올바른 경로 설정 필요
		auto path = PathFinder::Relative("CSV\\EventDesign_Template.csv");
        CSVReader rdrNew(path.string());
        for (const auto& row : rdrNew)
        {
            EventDefinition def;
            def.id = row["ID"].as<int>();
            def.name = HasColumn(row, "QuestName") ? row["QuestName"].as<std::string>() : std::string{};
            def.scene = HasColumn(row, "Scene") ? row["Scene"].as<std::string>() : std::string{};
            def.category = ParseCategory(HasColumn(row, "Category") ? row["Category"].as<std::string>() : "Quest");
            def.playerScope = ParsePlayerScope(HasColumn(row, "PlayerScope") ? row["PlayerScope"].as<std::string>() : "Shared");
            if (HasColumn(row, "AdvancePolicy")) 
            {
                def.advance = ParseAdvancePolicy(row["AdvancePolicy"].as<int>());
            }
            def.priorId = HasColumn(row, "PriorQuestID") ? row["PriorQuestID"].as<int>() : 0;
            def.nextId = HasColumn(row, "NextQuestID") ? row["NextQuestID"].as<int>() : 0;
            def.uiText = HasColumn(row, "Description") ? row["Description"].as<std::string>() : std::string{};
            if (HasColumn(row, "Objectives")) {
                ParseObjectivesDSL(row["Objectives"].as<std::string>(), def);
            }

            m_indexById[def.id] = m_definitions.size();
            m_definitions.emplace_back(std::move(def));
        }
        loaded = !m_definitions.empty();
    }
    catch (...) { /* fall back */ }
}

void EventManager::ResolveVariables(int id)
{
    auto it = m_runtime.find(id);
    if (it == m_runtime.end()) return;
    auto& rt = it->second;

    auto subst = [&](std::string& s)
    {
        if (s.empty()) return;
        std::string key;
        if (s.size() >= 3 && s[0] == '$' && s[1] == '{' && s.back() == '}') key = s.substr(2, s.size() - 3);
        else if (s[0] == '$') key = s.substr(1);
        if (!key.empty()) {
            auto kv = rt.vars.find(key);
            if (kv != rt.vars.end()) s = kv->second;
        }
    };

    for (auto& obj : rt.def.objectives)
    {
        subst(obj.targetTag);
        subst(obj.targetActor);
        subst(obj.ability);
        // triggerIndex via var (optional): allow $triggerIndex in params
        if (!obj.params.empty()) {
            auto itVar = obj.params.find("triggerIndex");
            if (itVar != obj.params.end()) {
                std::string s = itVar->second; subst(s);
                try { obj.triggerIndex = std::stoi(s); }
                catch (...) {}
            }
        }
    }
}

EventCategory EventManager::ParseCategory(const std::string& s)
{
    if (s == "UI") return EventCategory::UI;
    if (s == "Trigger") return EventCategory::Trigger;
    if (s == "non") return EventCategory::Non;
    return EventCategory::Quest;
}

PlayerScope EventManager::ParsePlayerScope(const std::string& s)
{
    if (s == "O") return PlayerScope::AllPlayersIndividually;
    if (s == "X") return PlayerScope::AnyPlayer;
    if (s == "non") return PlayerScope::None;
    return PlayerScope::Shared;
}

AdvancePolicy EventManager::ParseAdvancePolicy(int i)
{
    //std::string t = s; std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (i == 0) return AdvancePolicy::AutoAdvance;
    if (i == 1) return AdvancePolicy::WaitForNextTrigger;
        return AdvancePolicy::Manual; // default
}

ObjectiveType EventManager::ParseObjectiveType(const std::string& s)
{
    std::string t = s; std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    if (t == "destroy") return ObjectiveType::Destroy;
    if (t == "interact") return ObjectiveType::Interact;
    if (t == "eliminateall" || t == "eliminate" || t == "clear") return ObjectiveType::EliminateAll;
    if (t == "kill" || t == "killcount") return ObjectiveType::KillCount;
    if (t == "collect" || t == "collectcount") return ObjectiveType::CollectCount;
    if (t == "deliver") return ObjectiveType::Deliver;
    if (t == "survivenodebuff" || t == "survive") return ObjectiveType::SurviveNoDebuff;
    if (t == "useabilitytodestroy" || t == "abilitydestroy") return ObjectiveType::UseAbilityToDestroy;
    if (t == "reachtrigger" || t == "reach") return ObjectiveType::ReachTrigger;
    if (t == "purchase" || t == "buy") return ObjectiveType::Purchase;
    if (t == "timer" || t == "wait") return ObjectiveType::Timer;
    if (t == "changescene" || t == "scene") return ObjectiveType::ChangeScene;
    return ObjectiveType::Custom;
}

bool EventManager::ParseObjectivesDSL(const std::string& dsl, EventDefinition& out)
{
    auto parts = Split(dsl, ';');
    for (auto& raw : parts)
    {
        auto line = Trim(raw);
        if (line.empty()) continue;
        auto toks = TokenizePreserveQuotes(line);
        if (toks.empty()) continue;

        ObjectiveDef obj; obj.type = ParseObjectiveType(toks[0]);
        for (size_t i = 1; i < toks.size(); ++i)
        {
            auto t = toks[i];
            auto eq = t.find('=');
            if (eq == std::string::npos) continue;
            std::string k = Trim(t.substr(0, eq));
            std::string v = Trim(t.substr(eq + 1)); v = Unquote(v);
            std::string lk = k; std::transform(lk.begin(), lk.end(), lk.begin(), [](unsigned char c) {return (char)std::tolower(c); });

            if (lk == "tag" || lk == "target" || lk == "targettag") obj.targetTag = v;
            else if (lk == "actor" || lk == "actortag") obj.targetActor = v;
            else if (lk == "ability" || lk == "skill") obj.ability = v;
            else if (lk == "required" || lk == "req" || lk == "count") { try { obj.requiredCount = std::stoi(v); } catch (...) {} }
            else if (lk == "duration" || lk == "time" || lk == "seconds") { try { obj.durationSeconds = std::stof(v); } catch (...) {} }
            else if (lk == "index" || lk == "idx" || lk == "trigger" || lk == "triggerindex") { try { obj.triggerIndex = std::stoi(v); } catch (...) {} }
            else obj.params[k] = v;
        }
        out.objectives.push_back(std::move(obj));
    }
    return !out.objectives.empty();
}

void EventManager::EnsureRuntime(int id)
{
    if (m_runtime.find(id) != m_runtime.end()) return;

    auto* def = GetEventDefinition(id);
    if (!def) return;

    EventRuntime rt{};
    rt.def = *def;
    rt.status = EventStatus::NotStarted;
    rt.objectives.resize(def->objectives.size());


    m_runtime.emplace(id, std::move(rt));
}

bool EventManager::EvaluateAndMaybeComplete(int id)
{
    auto it = m_runtime.find(id);
    if (it == m_runtime.end()) return false;
    auto& rt = it->second;
    if (rt.status != EventStatus::Active) return false;

    const auto& players = m_activePlayers;

    // All objectives must be completed for this step
    for (size_t i = 0; i < rt.def.objectives.size(); ++i)
    {
        if (!ObjectiveEvaluator::IsCompleted(rt.def.objectives[i], rt.objectives[i], rt.def.playerScope, players))
            return false;
    }

    CompleteEvent(id);
    return true;
}

