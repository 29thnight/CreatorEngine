#pragma once
#include "MonoBehavior.h"
#include <unordered_set>
#include "Physics/Common.h"
#include <tuple>
#include "ObjectTypeMeta.h"

class InteractableComponent;
class TestComponent : public MonoBehavior, public virtual IRigidbody, public virtual ICollider
{
public:
	TestComponent();

	// Component��(��) ���� ��ӵ�
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;

	// Component��(��) ���� ��ӵ�
	virtual void Serialize(_inout json& out) override final;
	virtual void DeSerialize(_in json& in) override final;

	uint32 ID() override { return 0; }

public:
	float moveSpeed = 1.f;
	float jumpVelocity = 5.f;
	float gravity = -9.8f;

	DirectX::SimpleMath::Vector3 originPos;
	DirectX::SimpleMath::Vector3 direction;

	int playerID = 0;

	CharacterControllerAttribute attribute;
	physx::PxCapsuleController* controller = nullptr;
	physx::PxVec3 velocity = { 0.f, 0.f, 0.f };
	ContactReportCallback contactCallback;
	//physx::PxVec3 groundNormal = PxVec3(0, 1, 0); // �⺻ Normal (Y-Up)
	bool isGround = false;

	// MonoBehavior��(��) ���� ��ӵ�
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;

private:
	// ��ȣ�ۿ� �Ǵ� ĳ���Ͱ� �i�ƿ��� �׽�Ʈ
	std::unordered_set<InteractableComponent*> interactable;
	void AddInteract(InteractableComponent* inter);
	void RemoveInteract(InteractableComponent* inter);

public:
	// ICharacterController��(��) ���� ��ӵ�
	std::tuple<float, float, float> GetWorldPosition() override;
	std::tuple<float, float, float, float> GetWorldRotation() override;
	void SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot) override;
	void AddForce(float* velocity, ForceMode mode) override;
	void AddTorque(float* velocity, ForceMode mode) override;
	RigidbodyInfo* GetInfo() override;
	RigidbodyInfo info;

	// ICollider��(��) ���� ��ӵ�
	void SetLocalPosition(std::tuple<float, float, float> pos) override;
	void SetRotation(std::tuple<float, float, float, float> rotation) override;
	void SetIsTrigger(bool isTrigger) override;
	bool GetIsTrigger() override;
	Object* GetOwner() override;
	ColliderInfo collInfo;
};

template<>
struct MetaType<TestComponent>
{
	static constexpr std::string_view type{ "TestComponent" };
};