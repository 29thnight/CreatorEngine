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
	ItemType GetItemType() { return itemType; }
	//기본무기 검사(내구도 없음)
	bool IsBasic() { return itemType == ItemType::Basic; }
	//내구도 감소
	void DecreaseDur(bool isCharge = false);
	//내구도 검사관련 제공 메서드
	bool IsBroken() { return isBreak; }
	int GetCurDur() { return curDur; }
	int GetMaxDur() { return durMax; }

	bool CheckChargedDur(float chargedTime);

	int ItemTypeToInt() { return static_cast<int>(itemType); }
	ItemType IntToItemType(int type) { return static_cast<ItemType>(type); }

public:
	std::string itemName = "None";
	[[Property]]
	ItemType itemType = ItemType::Basic;

	//공격
	[[Property]]
	int itemAckDmg = 1;      //일반공격 데미지
	[[Property]]
	float itemAckRange = 2.0f; //일반공격 사정거리
	[[Property]]
	float itemKnockback = 0.05f; //일반 공격 넉백거리 (보류)
	[[Property]]
	float coopCrit = 2.0f;   //협동공격 데미지 배율
	
	//차징공격
	[[Property]]
	float chgTime = 0.2f; //키다운 유지시 n초당 내구도 감소
	[[Property]]
	int chgAckDmg = 5; //차징 공격시 데미지값  // 원거리무기는 이게 특수탄 데미지?
	[[Property]]
	float chgRange = 4.0f; //차징공격 사거리
	[[Property]]
	float chgKnockback = 0.05f; //차징공격 넉백거리
	[[Property]]
	int ChargeAttackBulletCount = 5;  //차지어택시 날아갈 탄환수
	[[Property]] 
	int ChargeAttackBulletAngle = 15;  //차지어택시 날아갈 탄환 사이의 각도


	//내구도
	[[Property]]
	int durMax = 10; //최대 내구도
	[[Property]]
	int durUseAtk = 1; //일반 공격시 소모 내구도
	int curDur = durMax;  //현재 내구도
	float chargingPersent = 0.f; //차징중인 시간/차징완료시간
	bool isBreak = false;   //무기 부서짐확인용
	bool isCompleteCharge = false; //차징완료 확인용


	//폭탄용
	[[Property]]
	float bombThrowDuration = 2.5f;
	[[Property]]
	float bombRadius = 2.5;
};
