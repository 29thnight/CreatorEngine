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
	//�ڽ� ��ü��� ������ �� UIǥ�ø� ó���ϱ� ���� �ʱ�ȭ �ܰ�
	auto children = GetOwner()->m_childrenIndices;
	for(int i = 0; i < WEAPON_GAUGE_MAX; ++i)
	{
		if (i >= children.size()) break;
		auto childObj = GameObject::FindIndex(children[i]);
		if (!childObj) break;
		m_slotGage[i] = childObj;
		//�ʱ�ȭ�� ��� ������ ��Ȱ��ȭ
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

	//���� ���Կ� �߰��� ���� ���� ����
	m_curWeaponType = (int)weapon->GetItemType();
	m_curDurability = weapon->GetCurDur();
	m_curMaxDurability = weapon->GetMaxDur();

	//���� Ÿ�Կ� �´� �̹����� ����
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
	//���� ���Կ� �߰��� ���� ���� ����
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
		//������ 0���Ϸ� �������� ������ ��Ȱ��ȭ
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
	//������ �ۼ�Ʈ ����
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
		//���� Ÿ���� None�̸� Ȱ��ȭ ��Ű�� ����
		return;
	}

	m_isActive = active;
	if (!m_isActive)
	{
		//��Ȱ��ȭ�� ��� ������ ��Ȱ��ȭ
		m_slotImage->SetTexture((int)m_curWeaponType);
		for (unsigned int i = 0; i < WEAPON_GAUGE_MAX; ++i)
		{
			if (!m_slotGage[i]) break;
			m_slotGage[i]->SetEnabled(false);
		}
	}
	else
	{
		//Ȱ��ȭ�� ���� ���� Ÿ�Կ� �´� �������� Ȱ��ȭ
		m_slotImage->SetTexture((int)m_curWeaponType + WEAPON_GAUGE_MAX);
		constexpr int convType = (int)ItemType::Basic;
		for (unsigned int i = 0; i < WEAPON_GAUGE_MAX; ++i)
		{
			if (!m_slotGage[i]) break; //0 Basic, 1 Melee, 2 Range, 3 Bomb
			int index = i + convType; //������ ������Ʈ ������ ������ Ÿ�� enum���� 1:1 ��Ī�� �ȵǹǷ� ����
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

