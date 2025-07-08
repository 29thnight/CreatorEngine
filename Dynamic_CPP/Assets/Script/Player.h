#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Player : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(Player)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}

	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;


	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}

	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override {}
	virtual void OnDestroy() override {}

	void Move(Mathf::Vector2 dir);
	void CatchAndThrow();
	void Catch();
	void Throw();
	void Attack();
	void SwapWeaponLeft();
	void SwapWeaponRight();

	int m_weaponIndex = 0;
	void Punch();


	void FindNearObject(GameObject* gameObject);
	float m_nearDistance = FLT_MAX;
	std::vector<GameObject*> m_weaponInventory;
	GameObject* m_curWeapon;
	GameObject* player = nullptr;
	GameObject* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
};
