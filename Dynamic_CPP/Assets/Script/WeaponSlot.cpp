#include "WeaponSlot.h"
#include "Weapon.h"
#include "ImageComponent.h"
#include "GameObject.h"
#include "pch.h"

enum class UIGageObjectOrder
{
	Basic,
	Melee,
	Range,
	Bomb,
};

void WeaponSlot::Awake()
{
	m_slotImage = GetOwner()->GetComponent<ImageComponent>();
	//자식 객체들로 게이지 및 UI표시를 처리하기 위한 초기화 단계
	auto children = GetOwner()->m_childrenIndices;
	for(int i = 0; i < WEAPON_GAUGE_MAX; ++i)
	{
		if (i >= children.size()) break;
		auto childObj = GameObject::FindIndex(children[i]);
		if (!childObj) break;
		m_slotGage[i] = childObj;
		//초기화시 모든 게이지 비활성화
		if (m_slotGage[i])
		{
			m_slotGage[i]->SetEnabled(false);
		}
	}
}

void WeaponSlot::Start()
{
	
}

void WeaponSlot::ApplyWeapon(Weapon* weapon)
{
	if (!weapon)
	{
		m_slotImage->SetTexture(0);
		m_curWeaponType = (int)ItemType::None;
		m_curDurability = 0;
		m_curMaxDurability = 0;
		SetActive(false);

		return;
	}

	//현재 슬롯에 추가된 무기 정보 갱신
	m_curWeaponType = (int)weapon->GetItemType();
	m_curDurability = weapon->GetCurDur();
	m_curMaxDurability = weapon->GetMaxDur();

	//무기 타입에 맞는 이미지로 변경
	if (!m_slotImage)
	{
		m_slotImage = GetOwner()->GetComponent<ImageComponent>();
	}

	int size = m_slotImage->GetTextures().size();
	if (m_curWeaponType >= size)
	{
		m_curWeaponType = (int)ItemType::Basic;
	}

	switch (m_curWeaponType)
	{
	case (int)ItemType::None:
		m_slotImage->SetTexture((int)ItemType::None);
		break;
	case (int)ItemType::Basic:
		m_slotImage->SetTexture((int)ItemType::Basic);
		break;
	case (int)ItemType::Melee:
		m_slotImage->SetTexture((int)ItemType::Melee);
		break;
	case (int)ItemType::Range:
		m_slotImage->SetTexture((int)ItemType::Range);
		break;
	case (int)ItemType::Bomb:
		m_slotImage->SetTexture((int)ItemType::Bomb);
		break;
	default:
		m_slotImage->SetTexture((int)ItemType::None);
		break;
	}
}

void WeaponSlot::UpdateDurability(Weapon* weapon)
{
	if (!weapon) return;
	//현재 슬롯에 추가된 무기 정보 갱신
	m_curDurability = weapon->GetCurDur();
	m_curMaxDurability = weapon->GetMaxDur();
	if (m_curMaxDurability > 0)
	{
		m_curPersent = static_cast<float>(m_curDurability) / static_cast<float>(m_curMaxDurability);
	}
	else
	{
		m_curPersent = 0.0f;
	}

	if (0 >= m_curDurability)
	{
		m_slotImage->SetTexture((int)ItemType::None);
		m_curWeaponType = (int)ItemType::None;
		//내구도 0이하로 떨어지면 게이지 비활성화
		for (unsigned int i = 0; i < WEAPON_GAUGE_MAX; ++i)
		{
			if (!m_slotGage[i]) break;
			m_slotGage[i]->SetEnabled(false);
		}
	}
}

void WeaponSlot::UpdateChargingPersent(Weapon* weapon)
{
	if (!weapon) return;
	//게이지 퍼센트 갱신
	m_curPersent = ( static_cast<float>(m_curDurability) 
		/ static_cast<float>(m_curMaxDurability)) * (1 - weapon->chargingPersent);
}

void WeaponSlot::EndChargingPersent()
{
	m_curPersent = (static_cast<float>(m_curDurability)
		/ static_cast<float>(m_curMaxDurability));
}

void WeaponSlot::SetActive(bool active)
{
	if(m_curWeaponType == (int)ItemType::None)
	{
		//무기 타입이 None이면 활성화 시키지 않음
		return;
	}

	m_isActive = active;
	if (!m_isActive)
	{
		//비활성화시 모든 게이지 비활성화
		m_slotImage->SetTexture((int)m_curWeaponType);
		for (unsigned int i = 0; i < WEAPON_GAUGE_MAX; ++i)
		{
			if (!m_slotGage[i]) break;
			m_slotGage[i]->SetEnabled(false);
		}
	}
	else
	{
		//활성화시 현재 무기 타입에 맞는 게이지만 활성화
		m_slotImage->SetTexture((int)m_curWeaponType + WEAPON_GAUGE_MAX);
		constexpr int convType = (int)ItemType::Basic;
		for (unsigned int i = 0; i < WEAPON_GAUGE_MAX; ++i)
		{
			if (!m_slotGage[i]) break; //0 Basic, 1 Melee, 2 Range, 3 Bomb
			int index = i + convType; //게이지 오브젝트 순서와 아이템 타입 enum값이 1:1 매칭이 안되므로 보정
			if (m_curWeaponType == index && 0 < m_curDurability)
			{
				m_slotGage[i]->SetEnabled(true);
			}
			else
			{
				m_slotGage[i]->SetEnabled(false);
			}
		}
	}
}

