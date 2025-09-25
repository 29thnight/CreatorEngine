#include "ItemManager.h"
#include "GameObject.h"
#include "GameManager.h"
#include "GameInstance.h"
#include "ItemComponent.h"
#include "ItemUIIcon.h"
#include "ItemUIPopup.h"
#include "pch.h"

void ItemManager::Start()
{
	itemSlots.fill(nullptr);

	gameManager = GetOwner()->GetComponent<GameManager>();
	gameManager->ApplyGlobalEnhancementsToAllPlayers();
	//TEST
	GameInstance::GetInstance()->CommonItemColor = CommonItemColor;
	GameInstance::GetInstance()->RareItemColor = RareItemColor;
	GameInstance::GetInstance()->EpicItemColor = EpicItemColor;

	//현재 씬의 아이템 슬롯 3개 세팅
	InitItemSlots();

	//아이템 슬롯 3개 세팅
	{
		auto slot0 = GameObject::Find("ItemSlot0");
		auto slot1 = GameObject::Find("ItemSlot1");
		auto slot2 = GameObject::Find("ItemSlot2");

		if (slot0)
			itemSlots[0] = slot0->GetComponent<ItemComponent>();
		if (slot1)
			itemSlots[1] = slot1->GetComponent<ItemComponent>();
		if (slot2)
			itemSlots[2] = slot2->GetComponent<ItemComponent>();
	}

	//아이템 아이콘 3개 세팅
	{
		auto popup0 = GameObject::Find("ItemIcon0");
		auto popup1 = GameObject::Find("ItemIcon1");
		auto popup2 = GameObject::Find("ItemIcon2");

		if (popup0)
			itemIcons[0] = popup0->GetComponent<ItemUIIcon>();
		if (popup1)
			itemIcons[1] = popup1->GetComponent<ItemUIIcon>();
		if (popup2)
			itemIcons[2] = popup2->GetComponent<ItemUIIcon>();
		//아이콘 세팅
		for (int i = 0; i < 3; ++i)
		{
			if (itemIcons[i])
			{
				itemIcons[i]->SetItemID(itemInfos[i].id);
				itemIcons[i]->SetRarityID(itemInfos[i].rarity);
				itemIcons[i]->SetTarget(itemSlots[i] ? itemSlots[i]->GetOwner() : nullptr);
			}
		}
	}

	//아이템 팝업 3개 세팅
	{
		auto popup0 = GameObject::Find("ItemPopup0");
		auto popup1 = GameObject::Find("ItemPopup1");
		auto popup2 = GameObject::Find("ItemPopup2");

		std::array<ItemUIPopup*, 3> itemPopups{};

		if (popup0)
			itemPopups[0] = popup0->GetComponent<ItemUIPopup>();
		if (popup1)
			itemPopups[1] = popup1->GetComponent<ItemUIPopup>();
		if (popup2)
			itemPopups[2] = popup2->GetComponent<ItemUIPopup>();
		//아이콘 세팅
		for (int i = 0; i < 3; ++i)
		{
			if (itemPopups[i])
			{
				itemPopups[i]->SetIconObject(itemIcons[i] ? itemIcons[i]->GetOwner() : nullptr);
			}
		}
	}
}

void ItemManager::Update(float tick)
{
	//TEST
	GameInstance::GetInstance()->CommonItemColor = CommonItemColor;
	GameInstance::GetInstance()->RareItemColor = RareItemColor;
	GameInstance::GetInstance()->EpicItemColor = EpicItemColor;
}

void ItemManager::InitItemSlots()
{
	auto* gi = GameInstance::GetInstance();
	int maxItemID = gi->GetMaxItemID();
	if (maxItemID <= 0)
		return;

	// 미적용 아이템 중에서 3개 랜덤 픽
	auto picked = gi->PickRandomUnappliedItems(3);

	// 초기화
	for (auto& info : itemInfos) info = ItemInfo{};

	// 3개 채우기
	for (size_t i = 0; i < picked.size() && i < itemInfos.size(); ++i)
	{
		itemInfos[i] = picked[i];
	}
}

void ItemManager::RefreshItemSlots()
{
	InitItemSlots();
	//아이템 슬롯 3개 세팅
	for (int i = 0; i < 3; ++i)
	{
		if (itemSlots[i])
		{
			//아이템 정보 세팅
			itemSlots[i]->m_itemID = itemInfos[i].id;
			itemSlots[i]->m_itemRarity = itemInfos[i].rarity;
			itemSlots[i]->name = itemInfos[i].name;
			itemSlots[i]->description = itemInfos[i].description;
			itemSlots[i]->price = itemInfos[i].price;
			itemSlots[i]->enhanceType = itemInfos[i].enhancementType;
			itemSlots[i]->enhanceValue = itemInfos[i].enhancementValue;
			itemSlots[i]->m_isItemSoldOut = false;
			// 아이템 아이콘 세팅
			if (itemIcons[i])
			{
				itemIcons[i]->SetItemID(itemInfos[i].id);
				itemIcons[i]->SetRarityID(itemInfos[i].rarity);
				itemIcons[i]->SetTarget(itemSlots[i] ? itemSlots[i]->GetOwner() : nullptr);
				itemIcons[i]->ResetPurchased();
			}
		}
	}
}

void ItemManager::BuyItem(int slotIndex)
{
	if(!gameManager)
		return;

	if (slotIndex < 0 || slotIndex >= static_cast<int>(itemSlots.size()))
		return;

	if (!itemSlots[slotIndex])
		return;

	const ItemInfo& info = itemInfos[slotIndex];
	int itemPrice = itemSlots[slotIndex]->price;

	// 이미 적용된(보유한) 아이템이면 막기
	if (GameInstance::GetInstance()->HasApplied(info.id, info.rarity))
	{
		// 이미 구매/적용됨 → 중복 방지
		// 여기서 UI 알림(“이미 보유 중”) 띄우거나 사운드만 재생
		return;
	}

	if (gameManager->GetReward() >= itemPrice)
	{
		//구매 가능
		gameManager->AddReward(-itemPrice);
		//아이템 획득 처리
		GameInstance::GetInstance()->ApplyItemEnhancement(info);
		gameManager->ApplyGlobalEnhancementsToAllPlayers();
		//아이템 슬롯 비우기
		ClearItemSlot(slotIndex);
	}
	else
	{
		//구매 불가
	}
}

void ItemManager::ClearItemSlot(int slotIndex)
{
	if (slotIndex < 0 || slotIndex >= static_cast<int>(itemSlots.size()))
		return;
	if (!itemSlots[slotIndex])
		return;
	//아이템 슬롯 비우기
	itemSlots[slotIndex]->m_itemID = -1;
	itemSlots[slotIndex]->m_itemRarity = 0;
	itemSlots[slotIndex]->name = "";
	itemSlots[slotIndex]->description = "";
	itemSlots[slotIndex]->price = 0;
	itemSlots[slotIndex]->enhanceType = 0;
	itemSlots[slotIndex]->enhanceValue = 0;
	itemSlots[slotIndex]->m_isItemSoldOut = true;
	//아이템 정보 비우기
	itemInfos[slotIndex] = ItemInfo{};
	//아이템 아이콘 비우기
	if (itemIcons[slotIndex])
	{
		itemIcons[slotIndex]->OnPurchased();
	}
}

