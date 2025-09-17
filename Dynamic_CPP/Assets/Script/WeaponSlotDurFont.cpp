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
					} //��... �̰� �³�?
				}
			}
		}
	}
}

void WeaponSlotDurFont::Update(float tick)
{
	if (!m_textComponent || !m_weaponSlot)
		return;

	//���� ������ ��Ȱ��ȭ ���¶�� ������Ʈ���� ����
	if (!m_weaponSlot->IsActive())
		return;

	//������ format���� message ����
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

