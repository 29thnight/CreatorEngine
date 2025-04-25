#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "RigidBodyComponent.generated.h"

enum class EBodyType
{
	STATIC,
	DYNAMIC,
	KINEMATIC
};
AUTO_REGISTER_ENUM(EBodyType)

class RigidBodyComponent : public Component 
{
public:
   ReflectRigidBodyComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(RigidBodyComponent)
	
	
	EBodyType GetBodyType() const { return m_bodyType; }
	void SetBodyType(const EBodyType& bodyType) { m_bodyType = bodyType; }
	
	Mathf::Vector3 GetLinearVelocity() const { return m_linearVelocity; }
	void SetLinearVelocity(const Mathf::Vector3& linearVelocity) { m_linearVelocity = linearVelocity; }
	void AddLinearVelocity(const Mathf::Vector3& linearVelocity) { m_linearVelocity += linearVelocity; }

	Mathf::Vector3 GetAngularVelocity() const { return m_angularVelocity; }
	void SetAngularVelocity(const Mathf::Vector3& angularVelocity) { m_angularVelocity = angularVelocity; }


	void SetLockLinearX(bool isLock) { m_isLockLinearX = isLock; }
	void SetLockLinearY(bool isLock) { m_isLockLinearY = isLock; }
	void SetLockLinearZ(bool isLock) { m_isLockLinearZ = isLock; }
	void SetLockAngularX(bool isLock) { m_isLockAngularX = isLock; }
	void SetLockAngularY(bool isLock) { m_isLockAngularY = isLock; }
	void SetLockAngularZ(bool isLock) { m_isLockAngularZ = isLock; }
	bool IsLockLinearX() const { return m_isLockLinearX; }
	bool IsLockLinearY() const { return m_isLockLinearY; }
	bool IsLockLinearZ() const { return m_isLockLinearZ; }
	bool IsLockAngularX() const { return m_isLockAngularX; }
	bool IsLockAngularY() const { return m_isLockAngularY; }
	bool IsLockAngularZ() const { return m_isLockAngularZ; }

	[[Property]]
	EBodyType m_bodyType = EBodyType::DYNAMIC;

private:

	
	Mathf::Vector3 m_linearVelocity;
	Mathf::Vector3 m_angularVelocity;


	bool m_isLockLinearX;
	bool m_isLockLinearY;
	bool m_isLockLinearZ;
	bool m_isLockAngularX;
	bool m_isLockAngularY;
	bool m_isLockAngularZ;
};

