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

	virtual void Attack(Player* _Owner, AttackContext _attackContext = {}) {}
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
	[[Property]]
	int ChargeAttackBulletCount = 5;
	[[Property]]
	int ChargeAttackBulletAngle = 15;


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
	bool isBreak = false;   //무기 부서짐확인용
	
	//아시스 -> 플레이어 날아올떄쓰는것들
	int OwnerPlayerIndex = -1;
	GameObject* ownerPlayer = nullptr; //

	
	
};
