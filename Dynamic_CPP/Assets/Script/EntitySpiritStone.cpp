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
	if (m_currentHP <= 0) return; // Ǯ���ؼ� ����ϱ� ���� ���� �̹� �ı��� ���¶�� ����.

	Player* player = dynamic_cast<Player*>(sender);
	if (player) {
		// �÷��̾ ������ ��쿡�� ó��.
		m_currentHP -= damage;
		if (m_criticalMark)
		{
			if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
			{
				damage *= player->m_curWeapon->coopCrit;
				hitinfo.isCritical = true;
				//������2��� hitEffect ũ��Ƽ�� ����Ʈ�� ��� ����,���ҽ� ����
			}
		}
		m_currentHP -= std::max(damage, 0);
		PlayHitEffect(this->GetOwner(), hitinfo);





		if(m_currentHP <= 0)
		{
			// �ı� ó��.
			m_currentHP = 0;

			// ��ȭ ����.
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
			// �������� �޾����� ���� ����ִ� ���.
			/*if (m_onDamageEvent.IsBound())
			{
				m_onDamageEvent.Invoke();
			}*/
		}
	}
}

