#include "ItemUIPopup.h"
#include "ItemUIIcon.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "Texture.h"
#include "GameInstance.h"
#include "pch.h"
void ItemUIPopup::Start()
{
	m_rect = m_pOwner->GetComponent<RectTransformComponent>();
	m_image = m_pOwner->GetComponent<ImageComponent>();
    m_popupSize = m_image->uiinfo.size;
}

void ItemUIPopup::Update(float tick)
{
	using namespace Mathf;

	if (!m_rect || !m_iconObj || !m_icon || !m_image || !m_iconRect) return;

    itemID = m_icon->GetItemID();
    rarityID = m_icon->GetRarityID();
    switch (rarityID)
    {
    case 2:
        m_image->color = GameInstance::GetInstance()->EpicItemColor;
        break;
    case 1:
        m_image->color = GameInstance::GetInstance()->RareItemColor;
        break;
    case 0:
    default:
        m_image->color = GameInstance::GetInstance()->CommonItemColor;
        break;
    }
    m_image->color.w = 0.5f;

	// ������ ��ġ ����ȭ
	auto iconPos = m_iconRect->GetAnchoredPosition();
    m_rect->SetAnchoredPosition(iconPos);

    // �������� �˾� �������� Ȯ��
    const bool popupActive = m_icon->IsSetPopup() && m_icon->IsPopupComplete();

    // ��ǥ ũ�� ����
    m_targetSize = popupActive ? m_popupSize : m_baseSize;

    // ���� ����
    m_popupElapsed += tick;
    float t = std::clamp(m_popupElapsed / m_duration, 0.f, 1.f);

    // EaseOutQuad ����
    float eased = Easing::EaseOutQuad(t);

    auto curSize = Mathf::Lerp(m_rect->GetSizeDelta(), m_targetSize, eased);
    m_rect->SetSizeDelta(curSize);

    // ��ǥ ���� �� Ÿ�̸� ����
    if (curSize == m_targetSize)
        m_popupElapsed = 0.f;

    m_prevPopupActive = popupActive;
}

void ItemUIPopup::SetIconObject(GameObject* iconObj)
{
    m_iconObj = iconObj;
    if (m_iconObj)
    {
        m_icon = m_iconObj->GetComponent<ItemUIIcon>();
		m_iconRect = m_iconObj->GetComponent<RectTransformComponent>();
	}
}

