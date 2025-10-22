#include "EventSelector.h"
#include "GameInstance.h"
#include "EventManager.h"
#include "TextComponent.h"
#include "StringHelper.h"
#include "pch.h"

void EventSelector::Start()
{
	m_textComponent = GetOwner()->GetComponent<TextComponent>();
}

void EventSelector::Update(float tick)
{
	if (!m_textComponent) return;

	auto eventMgr = GameInstance::GetInstance()->GetActiveEventManager();
	if (!eventMgr) return;
	auto activeIds = eventMgr->GetActiveEventIds();
	if (activeIds.empty())
	{
		m_textComponent->SetMessage("No Active Event");
		return;
	}

	std::string msg;
	for (int id : activeIds)
	{
		EventDefinition* def = eventMgr->GetEventDefinition(id);
		if (def)
		{
			m_textComponent->SetMessage(def->uiText);
		}
	}
}

