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

	void SetLockLinearX(bool isLock) { SetFlag(RB_LOCK_LIN_X, isLock); SetDirty(true); }
	void SetLockLinearY(bool isLock) { SetFlag(RB_LOCK_LIN_Y, isLock); SetDirty(true); }
	void SetLockLinearZ(bool isLock) { SetFlag(RB_LOCK_LIN_Z, isLock); SetDirty(true); }
	void SetLockAngularX(bool isLock) { SetFlag(RB_LOCK_ANG_X, isLock); SetDirty(true); }
	void SetLockAngularY(bool isLock) { SetFlag(RB_LOCK_ANG_Y, isLock); SetDirty(true); }
	void SetLockAngularZ(bool isLock) { SetFlag(RB_LOCK_ANG_Z, isLock); SetDirty(true); }

	void LockLinearXZ() { SetLockLinearX(true);  SetLockLinearZ(true); }
	void UnLockLinearXZ() { SetLockLinearX(false); SetLockLinearZ(false); }
	void LockAngularXYZ() { SetLockAngularX(true); SetLockAngularY(true); SetLockAngularZ(true); }
	void UnLockAngularXYZ() { SetLockAngularX(false); SetLockAngularY(false); SetLockAngularZ(false); }
	bool IsLockLinearX() const { return TestFlag(RB_LOCK_LIN_X); }
	bool IsLockLinearY() const { return TestFlag(RB_LOCK_LIN_Y); }
	bool IsLockLinearZ() const { return TestFlag(RB_LOCK_LIN_Z); }
	bool IsLockAngularX() const { return TestFlag(RB_LOCK_ANG_X); }
	bool IsLockAngularY() const { return TestFlag(RB_LOCK_ANG_Y); }
	bool IsLockAngularZ() const { return TestFlag(RB_LOCK_ANG_Z); }

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
	
	void DevelopOnlyDirtySet(bool dirty) { SetDirty(dirty); }
	bool IsRigidbodyDirty() const { return TestFlag(RB_DIRTY); }
private:
	// 모든 상태 변경을 PhysicsManager에 알리는 헬퍼 함수
	void NotifyPhysicsStateChange();
public:
	void NotifyPhysicsStateChange(const Mathf::Vector3& position);
private:
	[[Property]]
	EBodyType m_bodyType = EBodyType::DYNAMIC;

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
	Mathf::Vector3 velocity{};
private:
	Mathf::Vector3 m_linearVelocity;
	Mathf::Vector3 m_angularVelocity;

private:
	// ---- 비트 플래그 정의 ----
	enum : uint8_t {
		RB_LOCK_LIN_X = 1u << 0,
		RB_LOCK_LIN_Y = 1u << 1,
		RB_LOCK_LIN_Z = 1u << 2,
		RB_LOCK_ANG_X = 1u << 3,
		RB_LOCK_ANG_Y = 1u << 4,
		RB_LOCK_ANG_Z = 1u << 5,
		RB_DIRTY = 1u << 6, // isRigidbodyDirty
	};

	// 단일 바이트로 상태 보관 (초기값: dirty=false, 모든 lock=false)
	uint8_t m_rbFlags{ 0 };

	// ---- 헬퍼 ----
	inline bool TestFlag(uint8_t m) const noexcept {
		return (m_rbFlags & m) != 0;
	}
	inline void SetFlag(uint8_t m, bool v) noexcept {
		if (v) m_rbFlags |= m;
		else   m_rbFlags &= ~m;
	}
	inline void SetDirty(bool v) noexcept { SetFlag(RB_DIRTY, v); }
};
