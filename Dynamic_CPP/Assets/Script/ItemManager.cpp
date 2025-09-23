#include "ItemManager.h"
#include "GameObject.h"
#include "GameManager.h"
#include "ItemComponent.h"
#include "ItemUIIcon.h"
#include "pch.h"

void ItemManager::Start()
{
	itemSlots.fill(nullptr);

	gameManager = GetOwner()->GetComponent<GameManager>();
	gameManager->ApplyGlobalEnhancementsToAllPlayers();

	//���� ���� ������ ���� 3�� ����
	InitItemSlots();

	//������ ���� 3�� ����
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

	//������ ������ 3�� ����
	{
		auto popup0 = GameObject::Find("ItemPopup0");
		auto popup1 = GameObject::Find("ItemPopup1");
		auto popup2 = GameObject::Find("ItemPopup2");

		if (popup0)
			itemPopups[0] = popup0->GetComponent<ItemUIIcon>();
		if (popup1)
			itemPopups[1] = popup1->GetComponent<ItemUIIcon>();
		if (popup2)
			itemPopups[2] = popup2->GetComponent<ItemUIIcon>();
		//������ ����
		for (int i = 0; i < 3; ++i)
		{
			if (itemPopups[i])
			{
				itemPopups[i]->SetItemID(itemInfos[i].id);
				itemPopups[i]->SetTarget(itemSlots[i] ? itemSlots[i]->GetOwner() : nullptr);
			}
		}
	}

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

	// ������ ������ �߿��� 3�� ���� ��
	auto picked = gi->PickRandomUnappliedItems(3);

	// �ʱ�ȭ
	for (auto& info : itemInfos) info = ItemInfo{};

	// 3�� ä���
	for (size_t i = 0; i < picked.size() && i < itemInfos.size(); ++i)
	{
		itemInfos[i] = picked[i];
	}
}

void ItemManager::RefreshItemSlots()
{
	InitItemSlots();
	//������ ���� 3�� ����
	for (int i = 0; i < 3; ++i)
	{
		if (itemSlots[i])
		{
			//������ ���� ����
			itemSlots[i]->m_itemID = itemInfos[i].id;
			itemSlots[i]->m_itemRarity = itemInfos[i].rarity;
			itemSlots[i]->name = itemInfos[i].name;
			itemSlots[i]->description = itemInfos[i].description;
			itemSlots[i]->price = itemInfos[i].price;
			itemSlots[i]->enhanceType = itemInfos[i].enhancementType;
			itemSlots[i]->enhanceValue = itemInfos[i].enhancementValue;
			// ������ ������ ����
			if (itemPopups[i])
			{
				itemPopups[i]->SetItemID(itemInfos[i].id);
				itemPopups[i]->SetTarget(itemSlots[i] ? itemSlots[i]->GetOwner() : nullptr);
				itemPopups[i]->ResetPurchased();
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

	// �̹� �����(������) �������̸� ����
	if (GameInstance::GetInstance()->HasApplied(info.id, info.rarity))
	{
		// �̹� ����/����� �� �ߺ� ����
		// ���⼭ UI �˸�(���̹� ���� �ߡ�) ���ų� ���常 ���
		return;
	}

	if (gameManager->GetReward() >= itemPrice)
	{
		//���� ����
		gameManager->AddReward(-itemPrice);
		//������ ȹ�� ó��
		GameInstance::GetInstance()->ApplyItemEnhancement(info);
		gameManager->ApplyGlobalEnhancementsToAllPlayers();
		//������ ���� ����
		ClearItemSlot(slotIndex);
	}
	else
	{
		//���� �Ұ�
	}
}

void ItemManager::ClearItemSlot(int slotIndex)
{
	if (slotIndex < 0 || slotIndex >= static_cast<int>(itemSlots.size()))
		return;
	if (!itemSlots[slotIndex])
		return;
	//������ ���� ����
	itemSlots[slotIndex]->m_itemID = -1;
	itemSlots[slotIndex]->m_itemRarity = 0;
	itemSlots[slotIndex]->name = "";
	itemSlots[slotIndex]->description = "";
	itemSlots[slotIndex]->price = 0;
	itemSlots[slotIndex]->enhanceType = 0;
	itemSlots[slotIndex]->enhanceValue = 0;
	//������ ���� ����
	itemInfos[slotIndex] = ItemInfo{};
	//������ ������ ����
	if (itemPopups[slotIndex])
	{
		itemPopups[slotIndex]->OnPurchased();
	}
}

