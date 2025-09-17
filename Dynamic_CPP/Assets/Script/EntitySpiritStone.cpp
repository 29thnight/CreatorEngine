#include "EntitySpiritStone.h"
#include "pch.h"
#include "Player.h"
#include "GameManager.h"
#include "EntityAsis.h"
void EntitySpiritStone::Start()
{
}

void EntitySpiritStone::SendDamage(Entity* sender, int damage)
{
	if (m_currentHP <= 0) return; // Ǯ���ؼ� ����ϱ� ���� ���� �̹� �ı��� ���¶�� ����.

	Player* player = dynamic_cast<Player*>(sender);
	if (player) {
		// �÷��̾ ������ ��쿡�� ó��.
		m_currentHP -= damage;
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
			// �������� �޾����� ���� ����ִ� ���.
			/*if (m_onDamageEvent.IsBound())
			{
				m_onDamageEvent.Invoke();
			}*/
		}
	}
}

