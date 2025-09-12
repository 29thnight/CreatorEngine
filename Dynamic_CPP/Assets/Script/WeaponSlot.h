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
	//�ڽ� ��ü��� ������ �� UIǥ�ø� ó���Ѵ�.
	std::array<class GameObject*, WEAPON_GAUGE_MAX> m_slotGage{};
	//���� ���Կ� �߰��Ǿ� �ִ� �̹��� ������Ʈ(Ÿ�Ժ��� ������ �ؽ�ó �����)
	class ImageComponent* m_slotImage = nullptr;
	//���� ���Կ� �߰��� ���� Ÿ��(�̹��� Ÿ�Ժ��� �ؽ��� �ε��� �� switch�� ���� ����)
	int m_curWeaponType = (int)ItemType::None;
	//���� ���Կ� �߰��� ������ ������ �ִ�ġ(������ Ǯ����)
	int m_curMaxDurability = 0;
	//���� ���Կ� �߰��� ������ ������(������ ���ҿ�)
	int m_curDurability = 0;
	//UI Ȱ��ȭ ����
	bool m_isActive = false;
};
