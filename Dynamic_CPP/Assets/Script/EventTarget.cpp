#include "EventTarget.h"
#include "GameInstance.h"
#include "EventManager.h"

void EventTarget::Configure(int eventId, const std::string& varName, const std::string& value)
{
	m_eventId = eventId; m_varName = varName; m_value = value;
	if (m_mgr) m_mgr->SetEventVar(m_eventId, m_varName, m_value);
}

void EventTarget::Awake()
{
	Apply();
}

void EventTarget::Apply()
{
	if (!m_mgr)
	{
		auto mgr = GameInstance::GetInstance()->GetActiveEventManager();
		if (mgr) SetManager(mgr);
	}

	if (m_mgr && m_eventId != 0 && !m_value.empty())
		m_mgr->SetEventVar(m_eventId, m_varName, m_value);
}
