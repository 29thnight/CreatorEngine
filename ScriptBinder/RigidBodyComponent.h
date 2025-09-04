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
	
	void DevelopOnlyDirtySet(bool dirty) { isRigidbodyDirty = dirty; }
	bool IsRigidbodyDirty() const { return isRigidbodyDirty; }
private:
	// ��� ���� ������ PhysicsManager�� �˸��� ���� �Լ�
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
	bool m_useGravity = true; // �߷� ��� ����
	[[Property]]
	bool m_setTrigger = false; // Ʈ���� ���� ����
	[[Property]]
	bool m_setKinematic = false; // Ű�׸�ƽ ���� ����
	[[Property]]
	bool m_collisionEnabled = true; // �ݶ��̴� Ȱ��ȭ ����
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
