#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "Bullet.generated.h"


enum class BulletType
{
	Normal,
	Special,
};


class Player;
class EffectComponent;
class Bullet : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(Bullet)
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


	void Initialize(Player* owner, Mathf::Vector3 originpos, Mathf::Vector3 dir, int _damage);
	BulletType bulletType = BulletType::Normal;
	int m_damage = 0;
	//float rangedProjSpd = 1.0f; //발사체 이동속도
	//float rangedProjDist = 10.f; //발사체 최대 이동거리

	bool hasAttacked = false;
	float lifeTime = 5.f; //임시용
	Mathf::Vector3 m_moveDir = { 0,0,0 }; //나아갈 방향
	Player* m_ownerPlayer = nullptr;
	
private:
	bool beLateFrame = false; //setposion으로 정한 위치가 한프레임 늦어서 이렇게 조절
	bool OnEffect = false;
	EffectComponent* m_effect = nullptr;
};
