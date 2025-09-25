#include "ItemUIPopup.h"
#include "ItemUIIcon.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RectTransformComponent.h"
#include "Texture.h"
#include "ItemManager.h"
#include "GameInstance.h"
#include "pch.h"

void ItemUIPopup::Start()
{

    m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    m_image = m_pOwner->GetComponent<ImageComponent>();
    m_popupSize = m_image->uiinfo.size;
    auto gmObj = GameObject::Find("GameManager");
    if (gmObj)
    {
        m_itemManager = gmObj->GetComponent<ItemManager>();
    }
    //�˾� ���̾ƿ� ����
    //�ڽ� -> Ÿ��Ʋ(�Ϸ�)
    //Ÿ��Ʋ�� �ڽ� -> ����
    //������ �ڽ� -> ��ư
    //��ư�� �ڽ� -> ����
    // 0: Title, 1: Description, 2: Button, 3: Purchase
    GameObject* cur = GetOwner();
    for (int depth = 0; depth < 4; ++depth)
    {
        if (!cur || cur->m_childrenIndices.empty()) return;

        int childIdx = cur->m_childrenIndices[0]; // �׻� ù ��° �ڽĸ�
        cur = GameObject::FindIndex(childIdx);
        if (!cur) return;

        switch (depth)
        {
        case 0: 
            m_nameComp = cur->GetComponent<TextComponent>();
            break; // Title
        case 1: 
            m_descComp = cur->GetComponent<TextComponent>();
            break; // Description
        case 2: 
            m_button = cur->GetComponent<ImageComponent>(); 
            break; // Button
        case 3: 
            m_purchase = cur->GetComponent<ImageComponent>();  
            break; // Purchase
        }
    }

    if (m_purchase)
    {
        m_purchase->SetFloat2("centerUV", centerUV);
        m_purchase->SetFloat("radiusUV", radiusUV);
        m_purchase->SetFloat("percent", percent);
        m_purchase->SetFloat("startAngle", startAngle);
        m_purchase->SetInt("clockwise", clockwise);
        m_purchase->SetFloat("featherAngle", featherAngle);
        m_purchase->SetFloat4("tint", tint);
    }
}

void ItemUIPopup::Update(float tick)
{
	using namespace Mathf;

    m_lastDelta = tick;

	if (!m_rect     || !m_iconObj   || !m_icon      || !m_image     || 
        !m_iconRect || !m_button    || !m_purchase  || !m_descComp ||
        !m_nameComp) //������ ������ ���� ���� nullptr �˻� 
    {
        return;
    }

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
    {
        m_popupElapsed = 0.f;
    }

    if (m_popupSize == m_targetSize)
    {
        // �˾� ������ ��� �̹��� Ȱ��ȭ �ؽ�Ʈ Ȱ��ȭ
        m_nameComp->SetEnabled(true);
        m_descComp->SetEnabled(true);
        m_button->SetEnabled(true);
        m_purchase->SetEnabled(true);
        // ���� �ν��Ͻ����� ���� �޾ƿͼ� �ؽ�Ʈ �޼��� ����
        auto* itemInfo = GameInstance::GetInstance()->GetItemInfo(itemID, rarityID);
        if (itemInfo)
        {
            m_nameComp->SetMessage(itemInfo->name);
            m_descComp->SetMessage(itemInfo->description);
        }
        else
        {
            m_nameComp->SetMessage("");
            m_descComp->SetMessage("");
        }
    }
    else
    {
        //�ƴ� ��� ��Ȱ��ȭ
        m_nameComp->SetEnabled(false);
        m_descComp->SetEnabled(false);
        m_button->SetEnabled(false);
        m_purchase->SetEnabled(false);
        // ���� �ؽ�Ʈ �޼��� ����
        m_nameComp->SetMessage("");
        m_descComp->SetMessage("");
    }

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

void ItemUIPopup::PurshaseButton()
{
    if (!m_isCallStart) return;
    if (m_popupSize != m_targetSize) return;

    m_selectHold += m_lastDelta;

    // ���� �ۼ�Ʈ ��� (0~1)
    const float progress = (m_requiredSelectHold <= 0.f)
        ? 1.f
        : std::clamp(m_selectHold / m_requiredSelectHold, 0.f, 1.f);
    percent = progress;

    if (m_purchase)
    {
        m_purchase->SetFloat2("centerUV", centerUV);
        m_purchase->SetFloat("radiusUV", radiusUV);
        m_purchase->SetFloat("percent", percent);
        m_purchase->SetFloat("startAngle", startAngle);
        m_purchase->SetInt("clockwise", clockwise);
        m_purchase->SetFloat("featherAngle", featherAngle);
        m_purchase->SetFloat4("tint", tint);
    }

    if (!m_isSelectComplete && m_selectHold >= m_requiredSelectHold)
    {
        m_isSelectComplete = true;
        if (m_itemManager)
        {
            m_itemManager->BuyItem(itemID, rarityID);
        }
    }
}

void ItemUIPopup::ReleaseKey()
{
    m_selectHold = 0.f;
    percent = 0.f;                  // ������ ����
    if (m_purchase)
    {
        m_purchase->SetFloat2("centerUV", centerUV);
        m_purchase->SetFloat("radiusUV", radiusUV);
        m_purchase->SetFloat("percent", percent);
        m_purchase->SetFloat("startAngle", startAngle);
        m_purchase->SetInt("clockwise", clockwise);
        m_purchase->SetFloat("featherAngle", featherAngle);
        m_purchase->SetFloat4("tint", tint);
    }
}
