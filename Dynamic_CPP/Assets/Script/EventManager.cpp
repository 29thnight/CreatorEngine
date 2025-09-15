#include "EventManager.h"
#include "EventTarget.h"
#include "pch.h"

void EventManager::Awake()
{
    try
    {
        CSVReader reader("events.csv");
        for (const auto& row : reader)
        {
            EventDefinition def;
            def.id = row["ID"].as<int>();
            def.triggerType = static_cast<TriggerType>(row["TriggerType"].as<int>());
            def.targetType = static_cast<TargetType>(row["TargetType"].as<int>());
			def.parameters["EventTitle"] = row["EventTitle"].as<std::string>();
            m_definitions.emplace_back(std::move(def));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "EventManager::Awake - " << e.what() << '\n';
    }
}

void EventManager::Notify(int id)
{
    auto targetIt = m_targets.find(id);
    if (targetIt == m_targets.end())
    {
        std::cerr << "EventManager::Notify - target not found for id " << id << '\n';
        return;
    }

    auto defIt = std::find_if(m_definitions.begin(), m_definitions.end(),
        [id](const EventDefinition& def) { return def.id == id; });
    if (defIt == m_definitions.end())
    {
        std::cerr << "EventManager::Notify - definition not found for id " << id << '\n';
        return;
    }

    targetIt->second->Execute(*defIt);
}

void EventManager::RegisterTarget(int id, EventTarget* target)
{
    if (target == nullptr)
        return;
    m_targets[id] = target;
}

