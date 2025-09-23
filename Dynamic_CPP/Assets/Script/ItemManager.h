#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemComponent.h"
#include "GameInstance.h"
#include "ItemInfo.h"

class ItemManager : public ModuleBehavior
{
public:
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

	//���� ���� ������ ���� ���� �ʱ�ȭ �Լ�
	void InitItemSlots();
	//������ ���� ���� �Լ�
	void RefreshItemSlots();
	//������ ����	�Լ�
	void BuyItem(int slotIndex);
	//������ �� ���� �����
	void ClearItemSlot(int slotIndex);

private:
	class GameManager* gameManager{ nullptr };
	//���� ���� ������ ���� 3��
	std::array<ItemComponent*, 3> itemSlots;
	//������ ���� 3�� -> �������� ������ ���Կ� �־���
	std::array<ItemInfo, 3> itemInfos;
	//������ ������ 3��
	std::array<class ItemUIIcon*, 3> itemPopups;
};
