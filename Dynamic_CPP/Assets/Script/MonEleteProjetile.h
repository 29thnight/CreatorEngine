#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"

class Entity;
class EffectComponent;
class MonEleteProjetile : public ModuleBehavior
{
public:
	MODULE_BEHAVIOR_BODY(MonEleteProjetile)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	//이 친구는 날아가는 도중 충돌 검사 해야함
	//OnTriggerEnter,OnCollisionEnter 써야 하고 콜라이더 붙여야함
	bool isInitialize = false;
	bool m_isMoving = false;

	bool OwnerDestroy = false;
	bool isTrigger = false; //다중 충돌 안한다고 가정

	Entity* ownerEntity = nullptr;

	Mathf::Vector3 m_Dir = Mathf::Vector3::Zero;
	float m_Speed = 0.0f;
	int m_Damege = 0;

	void Initallize(Entity* owner, int damage, float speed, Mathf::Vector3 dir);
	void Action(GameObject* target);

	//벽이나 화면 밖으로 나가도 사라져야 하니 비활성화 따로 빼자
	void RevertPool();
private:
	EffectComponent* m_effect = nullptr;
};
