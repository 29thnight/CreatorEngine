#include "EntitySpiritStone.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "EntityAsis.h"
void EntitySpiritStone::Start()
{
	HitImpulseStart();
}

void EntitySpiritStone::Update(float tick)
{
	HitImpulseUpdate(tick);
}

void EntitySpiritStone::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	if (m_currentHP <= 0) return; // 풀링해서 사용하기 위해 만약 이미 파괴된 상태라면 무시.

	Player* player = dynamic_cast<Player*>(sender);
	if (player) {
		// 플레이어가 공격한 경우에만 처리.
		m_currentHP -= damage;
		if(m_currentHP <= 0)
		{
			// 파괴 처리.
			m_currentHP = 0;

			// 재화 지급.
			auto gameManager = GameObject::Find("GameManager");
			if (gameManager != nullptr) {
				GameManager* gm = gameManager->GetComponent<GameManager>();
				if (gm) {
					auto& asis = gm->GetAsis();
					for (auto& a : asis) {
						a->GetComponent<EntityAsis>()->AddPollutionGauge(m_stoneReward);
					}
				}
			}

			/*if (m_onDeathEvent.IsBound())
			{
				m_onDeathEvent.Invoke();
			}
			player->AddSpiritStone(m_stoneReward);*/
			GetOwner()->Destroy();
		}
		else
		{
			HitImpulse();
			// 데미지를 받았지만 아직 살아있는 경우.
			/*if (m_onDamageEvent.IsBound())
			{
				m_onDamageEvent.Invoke();
			}*/
		}
	}
}

