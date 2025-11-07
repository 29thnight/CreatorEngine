#include "ItemComponent.h"
#include "Player.h"
#include "ItemUIIcon.h"
#include "pch.h"
#include "GameManager.h"
void ItemComponent::Start()
{
    auto GMObj = GameObject::Find("GameManager");
    if (GMObj)
    {
        GM = GMObj->GetComponent<GameManager>();
    }

    auto childernIndex = GetOwner()->m_childrenIndices;
    for (auto childIndex : childernIndex)
    {
        auto Obj = GameObject::FindIndex(childIndex);
        if (Obj->m_tag == "Target")
        {
            m_Box = Obj;
        }
    }

}

void ItemComponent::OnTriggerEnter(const Collision& collision)
{
    if (collision.otherObj->HasComponent<Player>())
    {
        ++m_enterCount;
        auto player = collision.otherObj->GetComponent<Player>();
        if (player)
        {
            m_playerID = player->playerIndex;
        }
    }
}

void ItemComponent::OnTriggerExit(const Collision& collision)
{
    if (collision.otherObj->HasComponent<Player>())
    {
        --m_enterCount;
        m_playerID = -1;
        m_enterCount = std::max(0, m_enterCount);
    }
}

void ItemComponent::Update(float tick)
{

    if (GM && GM->GetReadyItemTutorial() == false) return;

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

