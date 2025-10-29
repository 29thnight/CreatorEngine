#include "ItemUIPopup.h"
#include "ItemUIIcon.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "RectTransformComponent.h"
#include "PlayerInput.h"
#include "Texture.h"
#include "ItemManager.h"
#include "EventManager.h"
#include "GameInstance.h"
#include "pch.h"

void ItemUIPopup::Start()
{
    m_rect = m_pOwner->GetComponent<RectTransformComponent>();
    m_image = m_pOwner->GetComponent<ImageComponent>();
    m_input = m_pOwner->GetComponent<PlayerInputComponent>();
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
    // 0: Title, 1: Description, 2: priceIcon, 3: PurchaseButton, 4: priceText, 5: PurchaseText
    constexpr int childMax = 6;

    GameObject* cur = GetOwner();
    for (int depth = 0; depth < childMax; ++depth)
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
        case 4:
            m_priceText = cur->GetComponent<TextComponent>();
            break;
        case 5:
            m_purchaseText = cur->GetComponent<TextComponent>();
            break;
        }
    }
}

void ItemUIPopup::Update(float tick)
{
    constexpr int layerOrderFoward = 10;

	using namespace Mathf;

    m_lastDelta = tick;
    //������ ������ ���� ���� nullptr �˻�
	if (!m_rect     || !m_iconObj   || !m_icon          || !m_image     || 
        !m_iconRect || !m_button    || !m_purchase      || !m_descComp  ||
        !m_nameComp || !m_input     || !m_purchaseText  || !m_priceText ) 
    {
        return;
    }

    if (0 <= m_icon->m_playerID)
    {
        m_input->controllerIndex = m_icon->m_playerID;
        m_input->SetEnabled(true);
    }
    else
    {
        m_input->SetEnabled(false);
    }

    itemID = m_icon->GetItemID();
    rarityID = m_icon->GetRarityID();
    switch (rarityID)
    {
    case 2:
        m_nameComp->SetColor(GameInstance::GetInstance()->EpicItemColor);
        break;
    case 1:
        m_nameComp->SetColor(GameInstance::GetInstance()->RareItemColor);
        break;
    case 0:
    default:
        m_nameComp->SetColor(GameInstance::GetInstance()->CommonItemColor);
        break;
    }

    switch (rarityID)
    {
    case 2:
        m_image->SetTexture((int)ItemRarity::Epic);
        break;
    case 1:
        m_image->SetTexture((int)ItemRarity::Rare);
        break;
    case 0:
    default:
        m_image->SetTexture((int)ItemRarity::Common);
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
        m_nameComp->GetOwner()->SetEnabled(true);

        // ���� �ν��Ͻ����� ���� �޾ƿͼ� �ؽ�Ʈ �޼��� ����
        auto* itemInfo = GameInstance::GetInstance()->GetItemInfo(itemID, rarityID);
        if (itemInfo)
        {
            m_nameComp->SetMessage(itemInfo->name);
            m_descComp->SetMessage(itemInfo->description);
            m_priceText->SetMessage(std::to_string(itemInfo->price));
        }
        else
        {
            m_nameComp->SetMessage("");
            m_descComp->SetMessage("");
            m_priceText->SetMessage("");
        }
    }
    else
    {
        //�ƴ� ��� ��Ȱ��ȭ
        m_nameComp->GetOwner()->SetEnabled(false);

        // ���� �ؽ�Ʈ �޼��� ����
        m_nameComp->SetMessage("");
        m_descComp->SetMessage("");
        m_priceText->SetMessage("");
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

    if (!m_isSelectComplete && m_selectHold >= m_requiredSelectHold)
    {
        m_isSelectComplete = true;
        if (m_itemManager)
        {
			if (eventID >= 0)
            {
                int itemSlot = m_itemManager->GetItemSlot(itemID, rarityID);
                m_itemManager->BuyItem(itemID, rarityID);

                if (m_itemManager->IsItemSoldOut(itemSlot))
                {
                    auto gmObj = m_itemManager->GetOwner();
                    if (gmObj)
                    {
                        auto Manager = gmObj->GetComponent<EventManager>();
                        if (Manager)
                        {
                            Manager->EmitPurchased("Item", 1, m_icon->m_playerID);
                        }
                    }
                }
            }
            else
            {
                m_itemManager->BuyItem(itemID, rarityID);
            }
        }
    }
}

void ItemUIPopup::ReleaseKey()
{
    m_selectHold = 0.f;
    percent = 0.f;                  // ������ ����
}

void ItemUIPopup::CalcLayerOrder(int add)
{
    //�̹���
    int popupLayer = m_image->GetLayerOrder();
    m_image->SetOrder(popupLayer + add);
    //�̸�
    int nameLayer = m_nameComp->GetLayerOrder();
    m_nameComp->SetOrder(nameLayer + add);
    //����
    int descLayer = m_descComp->GetLayerOrder();
    m_descComp->SetOrder(descLayer + add);
    //��ư
    int buttonLayer = m_button->GetLayerOrder();
    m_button->SetOrder(buttonLayer + add);
    //���Ź�ư
    int purchaseLayer = m_purchase->GetLayerOrder();
    m_purchase->SetOrder(purchaseLayer + add);
    //�����ؽ�Ʈ
    int priceLayer = m_priceText->GetLayerOrder();
    m_priceText->SetOrder(priceLayer + add);
    //�����ؽ�Ʈ
    int purchaseTextLayer = m_purchaseText->GetLayerOrder();
    m_purchaseText->SetOrder(purchaseTextLayer + add);
}
