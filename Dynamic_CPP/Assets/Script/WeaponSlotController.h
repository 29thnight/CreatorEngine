#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameInstance.h"
#include "WeaponSlotController.generated.h"

constexpr unsigned int WEAPON_SLOT_MAX = 4; //���� ���� �ִ� ����(0 Basic, 1 ~ 3 stack ���)

class WeaponSlotController : public ModuleBehavior
{
public:
   ReflectWeaponSlotController
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(WeaponSlotController)
	virtual void Awake() override;
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

	void SetCharacterIndex(int index) { m_characterIndex = index; }
	void AddWeapon(class Weapon* weapon, int slotIndex);
	void UpdateDurability(class Weapon* weapon, int slotIndex);
	void UpdateChargingPersent(class Weapon* weapon, int slotIndex);
	void EndChargingPersent(int slotIndex);
	void SetActive(int slotIndex);

	Core::DelegateHandle m_AddWeaponHandle{};
	Core::DelegateHandle m_ApplyWeaponHandle{};
	Core::DelegateHandle m_UpdateDurabilityHandle{};
	Core::DelegateHandle m_SetActiveHandle{};
	Core::DelegateHandle m_UpdateChargingPersentHandle{};
	Core::DelegateHandle m_EndChargingPersentHandle{};
	CharType m_charType = CharType::None;

private:
	[[Property]]
	int m_playerIndex = 0; //�÷��̾� �ε���
	//���� ���� ������Ʈ��
	std::array<class GameObject*, WEAPON_SLOT_MAX> m_weaponSlots{};
	//���� ���õ� ���� �ε���
	int m_curSelectIndex = 0;
	//�ɸ��� �ε���
	int m_characterIndex = 0; //0 ����, 1 ����(���� �ɸ��Ͱ� �߰��� ��츦 ����Ͽ� int������ ����)
	class ImageComponent* m_selectionImageComponent = nullptr;

};
