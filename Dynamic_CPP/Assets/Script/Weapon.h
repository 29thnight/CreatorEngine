#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Weapon.generated.h"
#include "ItemType.h"



class Player;
class Weapon : public ModuleBehavior
{
public:
   ReflectWeapon
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(Weapon)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void SetEnabled(bool able);
	std::string itemName = "None";
	[[Property]]
	int itemtype = 0;
	ItemType itemType = ItemType::Basic;

	//공격
	[[Property]]
	int itemAckDmg = 1;      //일반공격 데미지
	[[Property]]
	float itemAckSpd = 1.0f; //일반공격 속도
	[[Property]]
	float itemAckRange = 2.0f; //일반공격 사정거리
	[[Property]]
	float itemKnockback = 0.5f; //일반 공격 넉백거리 (보류)
	[[Property]]
	float coopCrit = 2.0f;   //협동공격 데미지 배율
	
	//차징공격
	[[Property]]
	float chgTime = 0.2f; //키다운 유지시 n초당 내구도 감소
	[[Property]]
	float chgDmgscal = 2.0f; //차징 공격시 딜보정값(합적용) //차징시간당 +=n?
	[[Property]]
	float chgSpd = 3.0f;  //차징공격속도  n초간 차징 다시못하는시간?
	[[Property]]
	float chgDelay = 1.0f; //차징공격 후딜레이 n초간 동작불가
	[[Property]]
	float chgRange = 4.0f; //차징공격 사거리
	[[Property]]
	float chgHitbox = 4.0f; //차징공격 넓이?
	[[Property]]
	float chgKnockback = 1.0f; //차징공격 넉백거리

	//내구도
	[[Property]]
	int durMax = 10; //최대 내구도
	[[Property]]
	int durUseAtk = 1; //일반 공격시 소모 내구도
	[[Property]]
	int durUseChg = 1; //차징시 시간당 소모 내구도
	[[Property]]
	int durUseBuf = 100; //버프사용시 소모 내구도(보류)
	int curDur = durMax;  //현재 내구도

	



	//아시스 -> 플레이어 날아올떄쓰는것들
	void Throw(Player* _player,Mathf::Vector3 statrPos);
	Mathf::Vector3 startPos{};
	Mathf::Vector3 endPos{};
	float timer = 0.f;
	float speed = 3.0f;
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //날아갈 경로찾는ㅇ요




	BuffType buffType = BuffType::None;
	int buffHitCount = 3;  //버프묻는 공격횟수
	float buffSpeed = 0.5f; 
	float buffRange = 1.0f; //아군에게 같이?
	float buffKnockbackPower = 0.15f; //일반공격 + 버프넉백
	int buffStackMax = 30; 
	
	
};
