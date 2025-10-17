#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "WeaponCapsule.generated.h"
#include "Entity.h"
class Player;
//아시스가 정화후 던져줄 웨폰캡슐 -> 플레이어에게 도달시 매칭된 weapon 생성해줌
class WeaponCapsule : public Entity
{
public:
   ReflectWeaponCapsule
	[[ScriptReflectionField]]
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
	int weaponCode = 0;  //asis가 넘겨줄거  0 1 2 3 값에따라 다른 무기생성 0번 베이직 1번 밀리 2번 레인지 3번 붐
	void CatchCapsule(Player* _player);
	void TryAddWeapon();
	void Throw(Player* _player, Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //날아갈 경로찾는용
private:
	[[Property]]
	float boundingRange = 2.f;  //둥둥떠다닐대 위아래 움직일거리
	[[Property]]
	float boundSpeed = 0.01f; //위아래 움직일속도
	float yBoundpos = 0.f;
	bool  goingUp = true; //위로뜨고있는지
};
