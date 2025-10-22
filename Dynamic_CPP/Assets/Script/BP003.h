#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class BP003 : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(BP003)
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

	Entity* m_ownerEntity = nullptr;

	int m_damage = 0;
	float m_radius = 0.0f;
	//pos를 가지고 있을까? 아님 보스에서 다 정해 줄까? 일단 보스가 전체적으로 다 정해주는걸로 
	//터지는 시간은 장판 패턴에 따라? 아니면 시간을 정해두고? 일단 정할수 있게 해두자
	float m_delay = 0.0f;
	float m_timer = 0.0f;

	bool isInitialize = false;
	bool isAttackStart = false;

	bool ownerDestory = false;

	//회전?? 자기 위치를 움직여야 하는군 그럼 플레그 받아서 돌아가는거 만들자
	bool isOrbiting = false;
	float m_orbitAngle = 0.0f;
	float m_orbitDistance = 0.0f;
	bool m_clockWise = true;

	bool m_useOrbiting = false;
	float m_orbitingStartDelay = 1.0f;
	float m_orbitingEndDelay = 1.0f;

	bool m_itemDrop = false;

	//음... 보스위치 기준으로 기준으로 하느냐? 춘식이 위치 기준으로 하는냐 인대... 이 친구들은 일단 보스 위치 기준으로 하자. 그럼 변수 추가 없어도 될듯

	//그럼 소유하는 엔티티랑 데미지,범위,시간 만 받으면 되려나?
	void Initialize(Entity* owner,Mathf::Vector3 pos, int damage, float radius, float delay, bool itemDrop, bool useOrbiting = false,bool clockwise =true);

	void Explosion(); //폭발하며 주변 대미지;
	void ItemDrop(); //아이템 드롭
};
