#pragma once
#include "Core.Minimal.h"
#include "Entity.h"
#include "EntityItem.generated.h"
#include "ItemType.h"
class Player;
class RigidBodyComponent;
enum EItemState
{
	NONE = 0,   // �Ƕ��� ����������
	ACQUIERED,  // ������ ȹ��
	CATCHED,    // �÷��̾�� ���������
	THROWN,     // ������ ����
	DROPPED,    // �÷��̾ ����ִٰ� ���������ϰ� ��������
	FALLED,     // ��� �ε����� �׳� ��������
	DESTROYED   // ������ �ı�
	// Add more item types as needed
};


class EntityItem : public Entity
{
public:
   ReflectEntityItem
	[[ScriptReflectionField]]
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

	void Drop(Mathf::Vector3 ownerForward,Mathf::Vector2 distacne);
	void Throw(Player* player,Mathf::Vector3 ownerForward,Mathf::Vector2 distacne,bool indicate);
	void SetThrowOwner(Player* player);
	Player* GetThrowOwner();
	void ClearThrowOwner();
public:
	[[Property]]
	GameObject* asisTail{ nullptr };
	[[Property]]
	RigidBodyComponent* m_rigid = nullptr;
	Mathf::Vector3 startPos{ 0.f, 0.f, 0.f };
	Mathf::Vector3 endPos{ 0.f, 0.f, 0.f };
	float throwDistacneX = 0.f; 
	float throwDistacneY = 0.f;
	float timer = 0.f;
	float speed = 2.f;
	

	bool canEat = false;
	[[Property]]
	float indicatorDistacne = 15.0f; 
	EItemState m_state = EItemState::FALLED;
	bool isTargettingTail = false;
	[[Property]]
	int  itemCode = 0;
	EItemType itemType = EItemType::Mushroom;

	bool OnGround;
private:
	Player* throwOwner{ nullptr }; // �� �������� ���� ��ü.
};
