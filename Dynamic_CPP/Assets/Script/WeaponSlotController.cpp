#include "WeaponSlotController.h"
#include "WeaponSlot.h"
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
	//TODO : 플레이어 오브젝트의 종류를(여성케릭인지 남성케릭인지) 인식해야함.
}

void WeaponSlotController::Update(float tick)
{
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

