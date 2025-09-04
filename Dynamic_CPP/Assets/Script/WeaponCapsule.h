#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Player;
//아시스가 정화후 던져줄 웨폰캡슐 -> 플레이어에게 도달시 매칭된 weapon 생성해줌
class WeaponCapsule : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(WeaponCapsule)
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


	//아시스 -> 플레이어 날아올떄쓰는것들
	int weaponCode = 0;  //asis가 넘겨줄거  0 1 2 값에따라 다른 무기생성
	void Throw(Player* _player, Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //날아갈 경로찾는용
};
