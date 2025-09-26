#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemComponent.h"
#include "GameInstance.h"
#include "ItemInfo.h"
#include "ItemManager.generated.h"

class ItemManager : public ModuleBehavior
{
public:
   ReflectItemManager
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(ItemManager)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	//현재 씬의 아이템 슬롯 최초 초기화 함수
	void InitItemSlots();
	//아이템 슬롯 리롤 함수
	void RefreshItemSlots();
	//아이템 구매	함수
	void BuyItem(int slotIndex);
	void BuyItem(int itemID, int rarityID);
	//아이템 빈 슬롯 만들기
	void ClearItemSlot(int slotIndex);

private:
	class GameManager* gameManager{ nullptr };
	//현재 씬의 아이템 슬롯 3개
	std::array<ItemComponent*, 3> itemSlots;
	//아이템 정보 3개 -> 렌덤으로 돌려서 슬롯에 넣어줌
	std::array<ItemInfo, 3> itemInfos;
	//아이템 아이콘 3개
	std::array<class ItemUIIcon*, 3> itemIcons;

	//테스트 전용
	[[Property]]
	Mathf::Color4 CommonItemColor{ 1.f, 1.f, 1.f, 1.f };
	[[Property]]
	Mathf::Color4 RareItemColor{ 1.f, 1.f, 1.f, 1.f };
	[[Property]]
	Mathf::Color4 EpicItemColor{ 1.f, 1.f, 1.f, 1.f };
};
