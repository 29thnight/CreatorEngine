#include "WeaponSlotController.h"
#include "WeaponSlot.h"
#include "ImageComponent.h"
#include "GameInstance.h"
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
	//TODO : 플레이어 오브젝트의 종류를(여성케릭인지 남성케릭인지) 인식해야함.
	if (!m_selectionImageComponent) return;

	constexpr int CONVERT_TYPE = 1;
	auto type = GameInstance::GetInstance()->GetPlayerCharType(PlayerDir(m_playerIndex + CONVERT_TYPE));

	if (type != CharType::None && m_playerIndex != (int)type - CONVERT_TYPE)
	{
		m_selectionImageComponent->SetTexture(1); //각각 반대되는 케릭터의 텍스처로 설정
		m_selectionImageComponent->uiEffects = UIEffects::UIEffects_FlipHorizontally;
	}
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

