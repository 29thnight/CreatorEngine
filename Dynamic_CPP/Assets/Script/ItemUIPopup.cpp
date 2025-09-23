#include "ItemUIPopup.h"
#include "ItemUIIcon.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "Texture.h"
#include "pch.h"
void ItemUIPopup::Start()
{
	m_rect = m_pOwner->GetComponent<RectTransformComponent>();
	m_image = m_pOwner->GetComponent<ImageComponent>();
    m_popupSize = m_image->uiinfo.size;

	//TEST : 강제로 아이콘 오브젝트 지정
	auto iconObj = GameObject::Find("TestIcon");
	SetIconObject(iconObj);
    //~TEST
}

void ItemUIPopup::Update(float tick)
{
	using namespace Mathf;
	//TODO : itemManager를 통해서 m_iconObj를 받아오도록 변경 필요
	if (!m_rect || !m_iconObj || !m_icon || !m_image || !m_iconRect) return;

	// 아이콘 위치 동기화
	auto iconPos = m_iconRect->GetAnchoredPosition();
    m_rect->SetAnchoredPosition(iconPos);

    // 아이콘이 팝업 상태인지 확인
    const bool popupActive = m_icon->IsSetPopup() && m_icon->IsPopupComplete();

    // 목표 크기 설정
    m_targetSize = popupActive ? m_popupSize : m_baseSize;

    // 보간 진행
    m_popupElapsed += tick;
    float t = std::clamp(m_popupElapsed / m_duration, 0.f, 1.f);

    // EaseOutQuad 적용
    float eased = Easing::EaseOutQuad(t);

    auto curSize = Mathf::Lerp(m_rect->GetSizeDelta(), m_targetSize, eased);
    m_rect->SetSizeDelta(curSize);

    // 목표 도달 시 타이머 리셋
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

