#include "ITriggerCondition.h"
#include "EventManager.h"

void ITriggerCondition::Raise(const EventSignal& sig)
{
	if (m_eventManager)
	{
		//m_eventManager->PushSignal(m_eventID, sig);
	}
}