#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "GameInstance.h"
#include "WeaponSlotController.generated.h"

constexpr unsigned int WEAPON_SLOT_MAX = 4; //무기 슬롯 최대 개수(0 Basic, 1 ~ 3 stack 운용)

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
	int m_playerIndex = 0; //플레이어 인덱스
	//무기 슬롯 오브젝트들
	std::array<class GameObject*, WEAPON_SLOT_MAX> m_weaponSlots{};
	//현재 선택된 슬롯 인덱스
	int m_curSelectIndex = 0;
	//케릭터 인덱스
	int m_characterIndex = 0; //0 남성, 1 여성(추후 케릭터가 추가될 경우를 대비하여 int형으로 선언)
	class ImageComponent* m_selectionImageComponent = nullptr;

};
