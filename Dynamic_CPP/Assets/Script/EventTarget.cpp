#include "EventTarget.h"
#include "GameInstance.h"
#include "Player.h"
#include "EventManager.h"
#include "pch.h"

void EventTarget::Configure(int eventId, const std::string& varName, const std::string& value)
{
	m_eventId = eventId; m_targetTag = varName; m_runtimeTag = value;
	if (m_mgr) 
	{
		m_mgr->SetEventVar(m_eventId, m_targetTag, m_runtimeTag);
	}
}

void EventTarget::Awake()
{
	Apply();
}

void EventTarget::OnTriggerEnter(const Collision& collision)
{
	if (!m_mgr) return;

}

void EventTarget::OnDestroy()
{
	if (!m_mgr)
	{
		auto mgr = GameInstance::GetInstance()->GetActiveEventManager();
		if (mgr) SetManager(mgr);
	}

	if (m_mgr && m_eventId != 0 && !m_runtimeTag.empty())
	{
		if (auto def = m_mgr->GetRuntimeDef(m_eventId)) 
		{                         
			bool sent = false;
			for (const auto& o : def->objectives) 
			{                               
				switch (o.type) 
				{
				case ObjectiveType::KillCount:
				case ObjectiveType::EliminateAll:
					// 적 처치 계열은 EnemyKilled 사용 (Evaluator가 sig.b를 봄)
					m_mgr->EmitEnemyKilled(m_runtimeTag, playerID);
					sent = true; 
					break;
				case ObjectiveType::Destroy:
				case ObjectiveType::CollectCount:      // 아이템 수집을 파괴/소멸로 모델링
				case ObjectiveType::Deliver:           // 전달 시 소멸한다면 동일 시그널 재사용
					m_mgr->EmitObjectDestroyed(m_runtimeTag, playerID);
					sent = true; 
					break;
				default: 
					break;
				}

				if (sent) 
				{
					return;
				}
			}
		}
	}
}

void EventTarget::Apply()
{
	if (!m_mgr)
	{
		auto mgr = GameInstance::GetInstance()->GetActiveEventManager();
		if (mgr) SetManager(mgr);
	}

	if (m_mgr && m_eventId != 0 && !m_targetTag.empty())
	{
		m_mgr->SetEventVar(m_eventId, m_targetTag, m_runtimeTag);
	}
}
