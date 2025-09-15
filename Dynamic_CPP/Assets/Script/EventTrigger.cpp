#include "pch.h"
#include "EventTrigger.h"
#include "EventManager.h"

void EventTrigger::Update(float tick)
{
    if (!m_triggered && m_manager)
    {
        m_manager->Notify(m_id);
        m_triggered = true;
    }
}

void EventTrigger::Init(int id, EventManager* manager)
{
	m_id = id;
	m_manager = manager;
}
