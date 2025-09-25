#include "ItemComponent.h"
#include "Player.h"
#include "ItemUIIcon.h"
#include "pch.h"

void ItemComponent::Start()
{
}

void ItemComponent::OnTriggerEnter(const Collision& collision)
{
    if (collision.otherObj->HasComponent<Player>())
    {
        ++m_enterCount;
    }
}

void ItemComponent::OnTriggerExit(const Collision& collision)
{
    if (collision.otherObj->HasComponent<Player>())
    {
        --m_enterCount;

        m_enterCount = std::max(0, m_enterCount);
    }
}

void ItemComponent::Update(float tick)
{
    if (!m_controlIcon) return;

    if (m_isItemSoldOut)
    {
        m_controlIcon->OnPurchased();
    }
    else
    {
        m_controlIcon->ResetPurchased();
    }
    
    if (0 >= m_enterCount)
    {
        m_controlIcon->SetPopup(false);
    }
    else
    {
        m_controlIcon->SetPopup(true);
    }

}

void ItemComponent::SetItemIcon(ItemUIIcon* comp)
{
    m_controlIcon = comp;
}

