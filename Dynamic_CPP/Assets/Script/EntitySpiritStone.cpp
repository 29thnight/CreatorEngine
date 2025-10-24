#include "EntitySpiritStone.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "EntityAsis.h"
#include "CriticalMark.h"
#include "PrefabUtility.h"
#include "EffectComponent.h"
#include "Weapon.h"
void EntitySpiritStone::Start()
{
	m_currentHP = maxHP;
	HitImpulseStart();

	auto owner = GetOwner();
	auto childred = owner->m_childrenIndices;
	for (auto& child : childred)
	{
		m_criticalMark = GameObject::FindIndex(child)->GetComponent<CriticalMark>();
	}
	if (!m_criticalMark)
	{
		m_criticalMark = GetOwner()->GetComponent<CriticalMark>();
	}
	auto newEffect = SceneManagers->GetActiveScene()->CreateGameObject("effect", GameObjectType::Empty, GetOwner()->m_index);
	m_effect = newEffect->AddComponent<EffectComponent>();
	m_effect->m_effectTemplateName = "resourceView";
	m_effect->Apply();
}

void EntitySpiritStone::Update(float tick)
{
	HitImpulseUpdate(tick);
	UpdateOutLine(tick);
}

void EntitySpiritStone::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	if (m_currentHP <= 0) return; // 풀링해서 사용하기 위해 만약 이미 파괴된 상태라면 무시.

	Player* player = dynamic_cast<Player*>(sender);
	if (player) {
		// 플레이어가 공격한 경우에만 처리.
		m_currentHP -= damage;
		if (m_criticalMark)
		{
			if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
			{
				damage *= player->m_curWeapon->coopCrit;
				hitinfo.isCritical = true;
				//데미지2배및 hitEffect 크리티컬 이펙트로 출력 몬스터,리소스 동일
			}
		}
		m_currentHP -= std::max(damage, 0);
		PlayHitEffect(this->GetOwner(), hitinfo);





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

