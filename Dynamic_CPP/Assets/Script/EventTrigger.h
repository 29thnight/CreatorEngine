#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include <cstdint>

class EventManager;

// Component responsible for notifying the EventManager when its condition is met.
class EventTrigger : public ModuleBehavior
{
public:
    MODULE_BEHAVIOR_BODY(EventTrigger)

    void Update(float tick) override;

    void Init(int id, EventManager* manager);
    void SetID(int id) { m_id = id; }
    void SetManager(EventManager* manager) { m_manager = manager; }

private:
    int m_id{};
    EventManager* m_manager{ nullptr };
    bool m_triggered{ false };
};