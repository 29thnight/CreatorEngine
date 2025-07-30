#pragma once
#include "Core.Minimal.h"
#include "Entity.h"

class Player;
class RigidBodyComponent;
enum EItemState
{
	NONE = 0,   // 맨땅에 떨어진상태
	ACQUIERED,  // 아이템 획득
	THROWN,     // 아이템 던짐
	DROPPED,    // 플레이어가 들고있다가 던지지못하고 떨궈질때
	DESTROYED   // 아이템 파괴
	// Add more item types as needed
};

class EntityItem : public Entity
{
public:
	MODULE_BEHAVIOR_BODY(EntityItem)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override;
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override;
	virtual void OnCollisionEnter(const Collision& collision) override;
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override;
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	void Drop(Mathf::Vector3 ownerForward, float distance);
	void Throw(Mathf::Vector3 ownerForward, float distance);
	void SetThrowOwner(Player* player);
	Player* GetThrowOwner();
	void ClearThrowOwner();
public:
	GameObject* asisTail{ nullptr };
	RigidBodyComponent* m_rigid = nullptr;
	bool isThrow = false;
	Mathf::Vector3 startPos{ 0.f, 0.f, 0.f };
	Mathf::Vector3 endPos{ 0.f, 0.f, 0.f };
	float throwDistacne = 6.f;
	float timer = 0.f;
	float speed = 2.f;
	EItemState m_state = EItemState::NONE;
private:
	Player* throwOwner{ nullptr }; // 이 아이템을 던진 객체.
};
