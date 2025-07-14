#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Player.generated.h"

class Player : public ModuleBehavior
{
public:
   ReflectPlayer
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Player)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}

	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override;
	virtual void OnTriggerExit(const Collision& collision) override;


	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override;
	virtual void OnCollisionExit(const Collision& collision) override;

	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override {}
	virtual void OnDestroy() override {}

	void Move(Mathf::Vector2 dir);
	void CatchAndThrow();
	void Catch();
	void Throw();
	void Dash();
	void Attack();
	void SwapWeaponLeft();
	void SwapWeaponRight();
	void AddWeapon(GameObject* weapon);
	void DeleteCurWeapon();  //쓰던무기 다쓰면 쓸꺼
	void DeleteWeapon(int index);
	void DeleteWeapon(GameObject* weapon);
	int m_weaponIndex = 0;
	void Punch();

	[[Property]]
	float HP = 0;
	[[Property]]
	float ThrowPowerX = 2.0;
	[[Property]]
	float ThrowPowerY = 10.0;
	void FindNearObject(GameObject* gameObject);
	float m_nearDistance = FLT_MAX;
	std::vector<GameObject*> m_weaponInventory;
	GameObject* m_curWeapon = nullptr;
	GameObject* player = nullptr;
	GameObject* catchedObject = nullptr;
	GameObject* m_nearObject = nullptr;
};
