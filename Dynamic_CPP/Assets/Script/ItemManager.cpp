#include "ItemManager.h"
#include "GameObject.h"
#include "GameManager.h"
#include "pch.h"

void ItemManager::Start()
{
	itemSlots.fill(nullptr);

	gameManager = GetOwner()->GetComponent<GameManager>();

	//현재 씬의 아이템 슬롯 3개 세팅
	InitItemSlots();

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

void ItemManager::Update(float tick)
{
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
			itemSlots[i]->m_itemID = itemInfos[i].id;
			itemSlots[i]->m_itemRarity = itemInfos[i].rarity;
			itemSlots[i]->name = itemInfos[i].name;
			itemSlots[i]->description = itemInfos[i].description;
			itemSlots[i]->price = itemInfos[i].price;
			itemSlots[i]->enhanceType = itemInfos[i].enhancementType;
			itemSlots[i]->enhanceValue = itemInfos[i].enhancementValue;
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
		//TODO : 아이템 획득 처리
		GameInstance::GetInstance()->ApplyItemEnhancement(info);
		//...
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
	itemSlots[slotIndex]->m_itemID = 0;
	itemSlots[slotIndex]->m_itemRarity = 0;
	itemSlots[slotIndex]->name = "";
	itemSlots[slotIndex]->description = "";
	itemSlots[slotIndex]->price = 0;
	itemSlots[slotIndex]->enhanceType = 0;
	itemSlots[slotIndex]->enhanceValue = 0;
}

