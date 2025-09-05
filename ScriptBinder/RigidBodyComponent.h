#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRegistableEvent.h"
#include "RigidBodyComponent.generated.h"
#include "EBodyType.h"
#include "EForceMode.h"
#include "../physics/PhysicsCommon.h"  

class RigidBodyComponent : public Component, public RegistableEvent<RigidBodyComponent>
{
public:
   ReflectRigidBodyComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(RigidBodyComponent)
	
   void Awake() override;
   void OnDestroy() override;
	
	EBodyType GetBodyType() const { return m_bodyType; }
	void SetBodyType(const EBodyType& bodyType);
	
	Mathf::Vector3 GetLinearVelocity() const { return m_linearVelocity; }
	void SetLinearVelocity(const Mathf::Vector3& linearVelocity) { m_linearVelocity = linearVelocity; }
	void AddLinearVelocity(const Mathf::Vector3& linearVelocity) { m_linearVelocity += linearVelocity; }

	Mathf::Vector3 GetAngularVelocity() const { return m_angularVelocity; }
	void SetAngularVelocity(const Mathf::Vector3& angularVelocity) { m_angularVelocity = angularVelocity; }

	void SetLockLinearX(bool isLock) { isRigidbodyDirty = true; m_isLockLinearX = isLock; }
	void SetLockLinearY(bool isLock) { isRigidbodyDirty = true; m_isLockLinearY = isLock; }
	void SetLockLinearZ(bool isLock) { isRigidbodyDirty = true; m_isLockLinearZ = isLock; }
	void SetLockAngularX(bool isLock) { isRigidbodyDirty = true; m_isLockAngularX = isLock; }
	void SetLockAngularY(bool isLock) { isRigidbodyDirty = true; m_isLockAngularY = isLock; }
	void SetLockAngularZ(bool isLock) { isRigidbodyDirty = true; m_isLockAngularZ = isLock; }

	void LockLinearXZ();
	void UnLockLinearXZ();
	void LockAngularXYZ();
	void UnLockAngularXYZ();
	bool IsLockLinearX() const { return m_isLockLinearX; }
	bool IsLockLinearY() const { return m_isLockLinearY; }
	bool IsLockLinearZ() const { return m_isLockLinearZ; }
	bool IsLockAngularX() const { return m_isLockAngularX; }
	bool IsLockAngularY() const { return m_isLockAngularY; }
	bool IsLockAngularZ() const { return m_isLockAngularZ; }

	void SetAngularDamping(float _AngularDamping = 0.05f);
	void SetLinearDamping(float _LinearDamping);
	void AddForce(const Mathf::Vector3& force, EForceMode forceMode = EForceMode::FORCE);
	void SetMass(float _mass);

	float GetMass() const { return m_mass; } // 자신의 질량을 직접 반환
	float GetMaxLinearVelocity() const { return maxLinearVelocity; } // 자신의 최대 선형 속도를 직접 반환
	float GetMaxAngularVelocity() const { return maxAngularVelocity; } // 자신의 최대 회전 속도를 직접 반환
	float GetMaxContactImpulse() const { return maxContactImpulse; } // 자신의 최대 접촉 임펄스를 직접 반환
	float GetMaxDepenetrationVelocity() const { return maxDepenetrationVelocity; } // 자신의 최대 침투 속도를 직접 반환

	float GetAngularDamping() const { return AngularDamping; } // 자신의 각 감쇠를 직접 반환
	float GetLinearDamping() const { return LinearDamping; } // 자신의 선형 감쇠를 직접 반환


	EForceMode GetForceMode() const { return forceMode; } // 자신의 힘 모드를 직접 반환
	void SetForceMode(EForceMode mode) { forceMode = mode; } // 힘 모드를 설정

	// Rigidbody의 키네마틱 상태를 설정합니다.
	void SetKinematic(bool isKinematic);
	bool IsKinematic() const { return m_setKinematic; } // 자신의 상태를 직접 반환

	// 콜라이더를 트리거 모드로 설정합니다.
	void SetIsTrigger(bool isTrigger);
	bool IsTrigger() const { return m_setTrigger; } // 자신의 상태를 직접 반환

	// 콜라이더 활성화/비활성화 (선택 사항, 필요시 추가)
	void SetColliderEnabled(bool enabled);
	bool IsColliderEnabled() const { return m_collisionEnabled; } // Component의 isEnabled 사용

	void UseGravity(bool useGravity);
	bool IsUsingGravity() const { return m_useGravity; } // 자신의 상태를 직접 반환
	
	void DevelopOnlyDirtySet(bool dirty) { isRigidbodyDirty = dirty; }
	bool IsRigidbodyDirty() const { return isRigidbodyDirty; }
private:
	// 모든 상태 변경을 PhysicsManager에 알리는 헬퍼 함수
	void NotifyPhysicsStateChange();
public:
	void NotifyPhysicsStateChange(const Mathf::Vector3& position);
private:
	[[Property]]
	EBodyType m_bodyType = EBodyType::DYNAMIC;

	Mathf::Vector3 velocity{};
	EForceMode forceMode{ EForceMode::NONE };
	float AngularDamping =0.05f;
	[[Property]]
	float LinearDamping = 0.01f;
	[[Property]]
	float m_mass = 70.f;
	[[Property]]
	float maxLinearVelocity = 1e+16;
	[[Property]]
	float maxAngularVelocity = 100.f;
	[[Property]]
	float maxContactImpulse = 1e+32;
	[[Property]]
	float maxDepenetrationVelocity = 1e+32;
	[[Property]]
	bool m_useGravity = true; // 중력 사용 여부
	[[Property]]
	bool m_setTrigger = false; // 트리거 설정 여부
	[[Property]]
	bool m_setKinematic = false; // 키네마틱 설정 여부
	[[Property]]
	bool m_collisionEnabled = true; // 콜라이더 활성화 여부
private:
	Mathf::Vector3 m_linearVelocity;
	Mathf::Vector3 m_angularVelocity;


private:
	bool m_isLockLinearX = false;
	bool m_isLockLinearY = false;
	bool m_isLockLinearZ = false;
	bool m_isLockAngularX = false;
	bool m_isLockAngularY = false;
	bool m_isLockAngularZ = false;

	bool isRigidbodyDirty = false;
};
