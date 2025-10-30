#include "WeaponSlotController.h"
#include "WeaponSlot.h"
#include "ImageComponent.h"
#include "pch.h"

void WeaponSlotController::Awake()
{
	if (0 == m_playerIndex)
	{
		m_weaponSlots[0] = GameObject::Find("P1_WeaponSlot(0)");
		m_weaponSlots[1] = GameObject::Find("P1_WeaponSlot(1)");
		m_weaponSlots[2] = GameObject::Find("P1_WeaponSlot(2)");
		m_weaponSlots[3] = GameObject::Find("P1_WeaponSlot(3)");
	}
	else if (1 == m_playerIndex)
	{
		m_weaponSlots[0] = GameObject::Find("P2_WeaponSlot(0)");
		m_weaponSlots[1] = GameObject::Find("P2_WeaponSlot(1)");
		m_weaponSlots[2] = GameObject::Find("P2_WeaponSlot(2)");
		m_weaponSlots[3] = GameObject::Find("P2_WeaponSlot(3)");
	}
}

void WeaponSlotController::Start()
{
	m_selectionImageComponent = GetComponent<ImageComponent>();
	//���� �ɸ��� ���� UI�� ����, ������ �������� �ʱ� �����Ǿ� ����.
	//�̶� �÷��̾� �ε����� 0�� ����, 1�� �����̹Ƿ� charType�� �ݴ�� ����
}

void WeaponSlotController::Update(float tick)
{
	if (!m_selectionImageComponent) return;

	if (0 == m_playerIndex)
	{
		if (m_charType == CharType::Man)
		{
			m_selectionImageComponent->SetTexture(1); //���� �ɸ��� �ؽ�ó�� ����
			m_selectionImageComponent->uiEffects = UIEffects::UIEffects_FlipHorizontally;
		}
	}
	else if (1 == m_playerIndex)
	{
		if (m_charType == CharType::Woman)
		{
			m_selectionImageComponent->SetTexture(1); //���� �ɸ��� �ؽ�ó�� ����
			m_selectionImageComponent->uiEffects = UIEffects::UIEffects_FlipHorizontally;
		}
	}
}

void WeaponSlotController::AddWeapon(Weapon* weapon, int slotIndex)
{
	if (nullptr == m_weaponSlots[slotIndex]) return;

	auto weaponSlot = m_weaponSlots[slotIndex]->GetComponent<WeaponSlot>();
	if (weaponSlot)
	{
		weaponSlot->ApplyWeapon(weapon);
	}
}

void WeaponSlotController::UpdateDurability(Weapon* weapon, int slotIndex)
{
	if (nullptr == m_weaponSlots[slotIndex]) return;

	auto weaponSlot = m_weaponSlots[slotIndex]->GetComponent<WeaponSlot>();
	if (weaponSlot)
	{
		weaponSlot->UpdateDurability(weapon);
	}
}

void WeaponSlotController::UpdateChargingPersent(Weapon* weapon, int slotIndex)
{
	if (nullptr == m_weaponSlots[slotIndex]) return;

	auto weaponSlot = m_weaponSlots[slotIndex]->GetComponent<WeaponSlot>();
	if (weaponSlot)
	{
		weaponSlot->UpdateChargingPersent(weapon);
	}
}

void WeaponSlotController::EndChargingPersent(int slotIndex)
{
	if (nullptr == m_weaponSlots[slotIndex]) return;
	auto weaponSlot = m_weaponSlots[slotIndex]->GetComponent<WeaponSlot>();
	if (weaponSlot)
	{
		weaponSlot->EndChargingPersent();
	}
}

void WeaponSlotController::SetActive(int slotIndex)
{
	for (unsigned int i = 0; i < WEAPON_SLOT_MAX; i++)
	{
		if (!m_weaponSlots[i]) break;

		auto weaponSlot = m_weaponSlots[i]->GetComponent<WeaponSlot>();
		if (!weaponSlot) break;

		if (i == slotIndex)
		{
			weaponSlot->SetActive(true);
		}
		else
		{
			weaponSlot->SetActive(false);
		}
	}

}

