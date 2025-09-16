#include "WeaponSlotDurFont.h"
#include "TextComponent.h"
#include "WeaponSlot.h"
#include "pch.h"
void WeaponSlotDurFont::Start()
{
	m_textComponent = GetOwner()->GetComponent<TextComponent>();
	int parentIdx = GetOwner()->m_parentIndex;
	if (parentIdx != GameObject::INVALID_INDEX)
	{
		auto parentObj = GameObject::FindIndex(parentIdx);
		if (parentObj)
		{
			int grandParentIdx = parentObj->m_parentIndex;
			if (grandParentIdx != GameObject::INVALID_INDEX)
			{
				auto grandParentObj = GameObject::FindIndex(grandParentIdx);
				if (grandParentObj)
				{
					int greatGrandParentIdx = grandParentObj->m_parentIndex;
					if (greatGrandParentIdx != GameObject::INVALID_INDEX)
					{
						auto greatGrandParentObj = GameObject::FindIndex(greatGrandParentIdx);
						if (greatGrandParentObj)
						{
							m_weaponSlot = greatGrandParentObj->GetComponent<WeaponSlot>();
						}
					} //아... 이게 맞나?
				}
			}
		}
	}
}

void WeaponSlotDurFont::Update(float tick)
{
	if (!m_textComponent || !m_weaponSlot)
		return;

	//무기 슬롯이 비활성화 상태라면 업데이트하지 않음
	if (!m_weaponSlot->IsActive())
		return;

	//내구도 format으로 message 설정
	int curDur = m_weaponSlot->GetCurrentDurability();
	int weapon_type = m_weaponSlot->GetCurrentWeaponType();
	std::string durStr = std::format("{}", curDur);
	switch (weapon_type)
	{
	case (int)ItemType::Melee:
	case (int)ItemType::Range:
	case (int)ItemType::Bomb:
		m_textComponent->SetMessage(durStr);
		break;
	case (int)ItemType::Basic:
	case (int)ItemType::None:
	default:
		m_textComponent->SetMessage("");
		break;
	}
}

