#pragma once
#include "MonoBehavior.h"
#include <unordered_set>
#include "Physics/Common.h"
#include <tuple>
#include "ObjectTypeMeta.h"

class InteractableComponent;
class CharacterController : public MonoBehavior, public virtual IRigidbody, public virtual ICollider
{
public:
	CharacterController();
	virtual ~CharacterController();

	// Component을(를) 통해 상속됨
	void Initialize() override;
	void FixedUpdate(float fixedTick) override;
	void Update(float tick) override;
	void LateUpdate(float tick) override;
	void EditorContext() override;

	// Component을(를) 통해 상속됨
	virtual void Serialize(_inout json& out) override final;
	virtual void DeSerialize(_in json& in) override final;

	uint32 ID() override { return 0; }

public:
	float moveSpeed = 1.f;
	float jumpVelocity = 5.f;
	float gravity = -9.8f;
	float rotationSpeed = 2000.f;
	float maxJumpVelocity = 10.f;

	float airFriction = 0.5f;

	DirectX::SimpleMath::Vector2 direction = { 0.f, 0.f };		// pad의 입력 방향 (입력이 있을 때만 값이 있음.)
	DirectX::SimpleMath::Vector2 currDirection = { 1.f, 0.f }; // 현재 보고있는 방향 (normalize됨)

	CharacterControllerAttribute attribute;

	void Jump();
	ContactReportCallback contactCallback;
private:
	//bool jumpflag = false;
	bool addForceChangeVelocityflag = false;
	physx::PxVec3 changeVelocity = { 0.f,0.f,0.f };
public:
	void AddForceChangeVelocity(float velocity[3]);
private:
	bool addForceLockWhenisGround = false;
	physx::PxVec3 changeLockVelocity = { 0.f,0.f,0.f };
	float lockTime = 0.f;
	float maxLockTime = 0.5f;
public:
	void AddForceChangeVelocityWhenisGround(float velocity[3]);
	void AddForceChangeVelocityWhenisGround(float velocity[3], float time);
private:
	physx::PxVec3 velocity = { 0.f, 0.f, 0.f };

public:
	physx::PxCapsuleController* controller = nullptr;
	CharacterController* otherhit = nullptr;

public:
	// MonoBehavior을(를) 통해 상속됨
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;
public:
	// IRigidbody을(를) 통해 상속됨
	std::tuple<float, float, float> GetWorldPosition() override;
	std::tuple<float, float, float, float> GetWorldRotation() override;
	void SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot) override;
	void AddForce(float* velocity, ForceMode mode) override;
	void AddTorque(float* velocity, ForceMode mode) override;
	RigidbodyInfo* GetInfo() override;
	RigidbodyInfo info;
public:
	// ICollider을(를) 통해 상속됨
	void SetLocalPosition(std::tuple<float, float, float> pos) override;
	void SetRotation(std::tuple<float, float, float, float> rotation) override;
	void SetIsTrigger(bool isTrigger) override;
	bool GetIsTrigger() override;
	Object* GetOwner() override;
	ColliderInfo collInfo;

private:
	float forceMovePosition[3] = { 0.f, 0.f, 0.f };
};

template<>
struct MetaType<CharacterController>
{
	static constexpr std::string_view type{ "CharacterController" };
};