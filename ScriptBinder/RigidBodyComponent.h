#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IAwakable.h"
#include "IOnDistroy.h"
#include "RigidBodyComponent.generated.h"
#include "EBodyType.h"

class RigidBodyComponent : public Component, public IAwakable, public IOnDistroy
{
public:
   ReflectRigidBodyComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(RigidBodyComponent)
	
   void Awake() override;
   void OnDistroy() override;
	
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

	void SetAngularDamping(float _AngularDamping = 0.05f);
	void SetLinearDamping(float _LinearDamping);
	void SetImpulseForce(const Mathf::Vector3& force);
	void SetMass(float _mass);
	[[Property]]
	EBodyType m_bodyType = EBodyType::DYNAMIC;

	DirectX::SimpleMath::Vector3 impulse{};
	bool shouldApplyImpulse = false; //순간적인 힘 받은게있는지
	float AngularDamping =0.05f;
	[[Property]]
	float LinearDamping = 0;
	[[Property]]
	float m_mass = 70.f;
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


};

