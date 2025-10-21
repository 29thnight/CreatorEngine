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

	float GetMass() const { return m_mass; } // �ڽ��� ������ ���� ��ȯ
	float GetMaxLinearVelocity() const { return maxLinearVelocity; } // �ڽ��� �ִ� ���� �ӵ��� ���� ��ȯ
	float GetMaxAngularVelocity() const { return maxAngularVelocity; } // �ڽ��� �ִ� ȸ�� �ӵ��� ���� ��ȯ
	float GetMaxContactImpulse() const { return maxContactImpulse; } // �ڽ��� �ִ� ���� ���޽��� ���� ��ȯ
	float GetMaxDepenetrationVelocity() const { return maxDepenetrationVelocity; } // �ڽ��� �ִ� ħ�� �ӵ��� ���� ��ȯ

	float GetAngularDamping() const { return AngularDamping; } // �ڽ��� �� ���踦 ���� ��ȯ
	float GetLinearDamping() const { return LinearDamping; } // �ڽ��� ���� ���踦 ���� ��ȯ


	EForceMode GetForceMode() const { return forceMode; } // �ڽ��� �� ��带 ���� ��ȯ
	void SetForceMode(EForceMode mode) { forceMode = mode; } // �� ��带 ����

	// Rigidbody�� Ű�׸�ƽ ���¸� �����մϴ�.
	void SetKinematic(bool isKinematic);
	bool IsKinematic() const { return m_setKinematic; } // �ڽ��� ���¸� ���� ��ȯ

	// �ݶ��̴��� Ʈ���� ���� �����մϴ�.
	void SetIsTrigger(bool isTrigger);
	bool IsTrigger() const { return m_setTrigger; } // �ڽ��� ���¸� ���� ��ȯ

	// �ݶ��̴� Ȱ��ȭ/��Ȱ��ȭ (���� ����, �ʿ�� �߰�)
	void SetColliderEnabled(bool enabled);
	bool IsColliderEnabled() const { return m_collisionEnabled; } // Component�� isEnabled ���

	void UseGravity(bool useGravity);
	bool IsUsingGravity() const { return m_useGravity; } // �ڽ��� ���¸� ���� ��ȯ
	
	void DevelopOnlyDirtySet(bool dirty) { SetDirty(dirty); }
	bool IsRigidbodyDirty() const { return TestFlag(RB_DIRTY); }
private:
	// ��� ���� ������ PhysicsManager�� �˸��� ���� �Լ�
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
	bool m_useGravity = true; // �߷� ��� ����
	[[Property]]
	bool m_setTrigger = false; // Ʈ���� ���� ����
	[[Property]]
	bool m_setKinematic = false; // Ű�׸�ƽ ���� ����
	[[Property]]
	bool m_collisionEnabled = true; // �ݶ��̴� Ȱ��ȭ ����
	Mathf::Vector3 velocity{};
private:
	Mathf::Vector3 m_linearVelocity;
	Mathf::Vector3 m_angularVelocity;

private:
	// ---- ��Ʈ �÷��� ���� ----
	enum : uint8_t {
		RB_LOCK_LIN_X = 1u << 0,
		RB_LOCK_LIN_Y = 1u << 1,
		RB_LOCK_LIN_Z = 1u << 2,
		RB_LOCK_ANG_X = 1u << 3,
		RB_LOCK_ANG_Y = 1u << 4,
		RB_LOCK_ANG_Z = 1u << 5,
		RB_DIRTY = 1u << 6, // isRigidbodyDirty
	};

	// ���� ����Ʈ�� ���� ���� (�ʱⰪ: dirty=false, ��� lock=false)
	uint8_t m_rbFlags{ 0 };

	// ---- ���� ----
	inline bool TestFlag(uint8_t m) const noexcept {
		return (m_rbFlags & m) != 0;
	}
	inline void SetFlag(uint8_t m, bool v) noexcept {
		if (v) m_rbFlags |= m;
		else   m_rbFlags &= ~m;
	}
	inline void SetDirty(bool v) noexcept { SetFlag(RB_DIRTY, v); }
};
