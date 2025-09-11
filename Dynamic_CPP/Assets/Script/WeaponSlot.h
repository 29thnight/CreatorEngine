#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "ItemType.h"

constexpr unsigned int WEAPON_GAUGE_MAX = 4;

class WeaponSlot : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(WeaponSlot)
	virtual void Awake() override;
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override {};
	virtual void LateUpdate(float tick) override {};
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void ApplyWeapon(class Weapon* weapon);
	void UpdateDurability(class Weapon* weapon);
	void SetActive(bool active);
	bool IsActive() const { return m_isActive; }

	int GetCurrentWeaponType() const { return m_curWeaponType; }
	int GetCurrentDurability() const { return m_curDurability; }
	int GetMaxDurability() const { return m_curMaxDurability; }

private:
	//자식 객체들로 게이지 및 UI표시를 처리한다.
	std::array<class GameObject*, WEAPON_GAUGE_MAX> m_slotGage{};
	//현재 슬롯에 추가되어 있는 이미지 컴포넌트(타입별로 렌더할 텍스처 변경용)
	class ImageComponent* m_slotImage = nullptr;
	//현재 슬롯에 추가된 무기 타입(이미지 타입별로 텍스쳐 인덱스 값 switch문 돌릴 예정)
	int m_curWeaponType = (int)ItemType::None;
	//현재 슬롯에 추가된 무기의 내구도 최대치(게이지 풀업용)
	int m_curMaxDurability = 0;
	//현재 슬롯에 추가된 무기의 내구도(게이지 감소용)
	int m_curDurability = 0;
	//UI 활성화 여부
	bool m_isActive = false;
};
