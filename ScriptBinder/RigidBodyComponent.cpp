#include "RigidBodyComponent.h"
#include "SceneManager.h"
#include "Scene.h"

void RigidBodyComponent::Awake()
{
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->CollectRigidBodyComponent(this);
	}
}

void RigidBodyComponent::OnDestroy()
{
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->UnCollectRigidBodyComponent(this);
	}
}

void RigidBodyComponent::LockLinearXZ()
{
	m_isLockLinearX = true;
	m_isLockLinearZ = true;
}

void RigidBodyComponent::UnLockLinearXZ()
{
	m_isLockLinearX = false;
	m_isLockLinearZ = false;
}

void RigidBodyComponent::LockAngularXYZ()
{
	m_isLockAngularX = true;
	m_isLockAngularY = true;
	m_isLockAngularZ = true;
}

void RigidBodyComponent::UnLockAngularXYZ()
{
	m_isLockAngularX = false;
	m_isLockAngularY = false;
	m_isLockAngularZ = false;
}

void RigidBodyComponent::SetAngularDamping(float _AngularDamping)
{
	AngularDamping = _AngularDamping;
}

void RigidBodyComponent::SetLinearDamping(float _LinearDamping)
{
	LinearDamping = _LinearDamping;
}

void RigidBodyComponent::AddForce(const Mathf::Vector3& force, EForceMode mode)
{
	forceMode = mode;
	velocity = force;
}

void RigidBodyComponent::SetMass(float _mass)
{
	m_mass = _mass;
}

void RigidBodyComponent::SetKinematic(bool isKinematic)
{
	PhysicsManagers->SetRigidBodyKinematic(GetInstanceID(), isKinematic);
}

bool RigidBodyComponent::IsKinematic() const
{
	return PhysicsManagers->IsRigidBodyKinematic(GetInstanceID());
}

void RigidBodyComponent::SetIsTrigger(bool isTrigger)
{
	PhysicsManagers->SetRigidBodyIsTrigger(GetInstanceID(), isTrigger);
}

bool RigidBodyComponent::IsTrigger() const
{
	return PhysicsManagers->IsRigidBodyTrigger(GetInstanceID());
}

void RigidBodyComponent::SetColliderEnabled(bool enabled)
{
	PhysicsManagers->SetRigidBodyColliderEnabled(GetInstanceID(), enabled);
}

bool RigidBodyComponent::IsColliderEnabled() const
{
	return PhysicsManagers->IsRigidBodyColliderEnabled(GetInstanceID());
}