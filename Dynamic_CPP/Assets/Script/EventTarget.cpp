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
					// �� óġ �迭�� EnemyKilled ��� (Evaluator�� sig.b�� ��)
					m_mgr->EmitEnemyKilled(m_runtimeTag, playerID);
					sent = true; 
					break;
				case ObjectiveType::Destroy:
				case ObjectiveType::CollectCount:      // ������ ������ �ı�/�Ҹ�� �𵨸�
				case ObjectiveType::Deliver:           // ���� �� �Ҹ��Ѵٸ� ���� �ñ׳� ����
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
