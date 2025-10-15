#include "ReachEventTrigger.h"
#include "EventManager.h"
#include "GameInstance.h"
#include "Player.h"
#include "pch.h"

void ReachEventTrigger::Awake()
{
	EnsureManager();
	m_alreadyEmitted = false;
}

void ReachEventTrigger::OnTriggerEnter(const Collision& collision)
{
	if (!ShouldEmitOnEnter()) return;
	EmitIfAllowed(collision);
}

void ReachEventTrigger::OnTriggerStay(const Collision& collision)
{
	if (!ShouldEmitOnStay()) return;
	EmitIfAllowed(collision);
}

void ReachEventTrigger::OnTriggerExit(const Collision& collision)
{
	if (!ShouldEmitOnExit()) return;
	EmitIfAllowed(collision);
}

bool ReachEventTrigger::EmitIfAllowed(const Collision& c)
{
	if (m_once && m_alreadyEmitted) return false;
	EnsureManager();
	if (!m_mgr) return false;

	const std::string actorTag = ResolveActorTag(c); // 충돌 객체의 Tag
	int currPlayerID{ -1 };

	if(!m_allPlayerPass)
	{
		if (c.otherObj->HasComponent<Player>())
		{
			auto playerComp = c.otherObj->GetComponentDynamicCast<Player>();
			if (playerComp)
			{
				if (-1 != m_playerId && playerComp->playerIndex == m_playerId)
				{
					currPlayerID = playerComp->playerIndex;
				}
			}
		}

		m_mgr->EmitReachedTrigger(actorTag, m_triggerIndex, currPlayerID);
	}
	else
	{
		if (c.otherObj->HasComponent<Player>())
		{
			auto playerComp = c.otherObj->GetComponentDynamicCast<Player>();
			if (playerComp)
			{
				currPlayerID = playerComp->playerIndex;
			}
		}

		m_mgr->EmitReachedTrigger(actorTag, m_triggerIndex, currPlayerID);
	}

	if (m_once) m_alreadyEmitted = true;
	return true;
}


std::string ReachEventTrigger::ResolveActorTag(const Collision& c) const
{
	// 항상 충돌 주체의 Tag 사용. 없으면 빈 문자열(와일드카드)
	return (c.otherObj ? c.otherObj->m_tag.ToString() : std::string{});
}


void ReachEventTrigger::EnsureManager()
{
	if (!m_mgr) 
	{
		if (auto* gi = GameInstance::GetInstance()) 
		{
			m_mgr = gi->GetActiveEventManager();
		}
	}
}