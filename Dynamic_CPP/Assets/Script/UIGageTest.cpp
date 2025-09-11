#include "UIGageTest.h"
#include "ImageComponent.h"
#include "WeaponSlot.h"
#include "pch.h"

void UIGageTest::Start()
{
	m_imageComponent = GetOwner()->GetComponent<ImageComponent>();
	int parentIdx = GetOwner()->m_parentIndex;
	if (parentIdx != GameObject::INVALID_INDEX)
	{
		auto parentObj = GameObject::FindIndex(parentIdx);
		if (parentObj)
		{
			m_weaponSlot = parentObj->GetComponent<WeaponSlot>();
		}
	}

	if (!m_imageComponent)
	{
		std::cout << "UIGageTest: ImageComponent not found!" << std::endl;
		return;
	}

	m_imageComponent->SetFloat2("centerUV", centerUV);
	m_imageComponent->SetFloat("radiusUV", radiusUV);
	m_imageComponent->SetFloat("percent", percent);
	m_imageComponent->SetFloat("startAngle", startAngle);
	m_imageComponent->SetInt("clockwise", clockwise);
	m_imageComponent->SetFloat("featherAngle", featherAngle);
	m_imageComponent->SetFloat4("tint", tint);
}

void UIGageTest::Update(float tick)
{
	if (!m_imageComponent)
		return;

	//무기 슬롯이 비활성화 상태라면 업데이트하지 않음
	if (m_weaponSlot && !m_weaponSlot->IsActive())
		return;

	//내구도 퍼센트 계산
	if (m_weaponSlot)
	{
		int curDur = m_weaponSlot->GetCurrentDurability();
		int maxDur = m_weaponSlot->GetMaxDurability();
		if (maxDur > 0)
		{
			percent = static_cast<float>(curDur) / static_cast<float>(maxDur);
		}
		else
		{
			percent = 0.f;
		}
	}
	else
	{
		percent = 0.f;
	}

	m_imageComponent->SetFloat2("centerUV", centerUV);
	m_imageComponent->SetFloat("radiusUV", radiusUV);
	m_imageComponent->SetFloat("percent", percent);
	m_imageComponent->SetFloat("startAngle", startAngle);
	m_imageComponent->SetInt("clockwise", clockwise);
	m_imageComponent->SetFloat("featherAngle", featherAngle);
	m_imageComponent->SetFloat4("tint", tint);
}

