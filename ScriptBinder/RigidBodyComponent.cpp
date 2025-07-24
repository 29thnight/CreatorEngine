#include "RigidBodyComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "../Physics/ICollider.h"

void RigidBodyComponent::Awake()
{
	std::cout << "RigidBodyComponent::Awake() - InstanceID: " << GetOwner()->GetInstanceID() << std::endl;
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->CollectRigidBodyComponent(this);
	}
	// 자신의 GameObject에 있는 모든 ColliderComponent를 찾아 상태를 동기화합니다.
	auto colliders = GetOwner()->GetComponents<ICollider>();
	for (auto& collider : colliders)
	{
		if (collider)
		{
			collider->SetColliderType(m_setTrigger ? EColliderType::TRIGGER : EColliderType::COLLISION);
		}
	}
}

void RigidBodyComponent::OnDestroy()
{
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->UnCollectRigidBodyComponent(this);
	}
	Physics->DestroyActor(GetOwner()->GetInstanceID()); // PhysX 액터 제거 요청
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

void RigidBodyComponent::SetBodyType(const EBodyType& bodyType)
{
	m_bodyType = bodyType;
	NotifyPhysicsStateChange();
}

void RigidBodyComponent::SetKinematic(bool isKinematic)
{
	m_setKinematic = isKinematic;
	NotifyPhysicsStateChange();
}

void RigidBodyComponent::SetIsTrigger(bool isTrigger)
{
	m_setTrigger = isTrigger;
	// Collider의 상태를 동기화합니다.
	auto colliders = GetOwner()->GetComponents<ICollider>();
	for (auto& collider : colliders)
	{
		if (collider)
		{
			collider->SetColliderType(isTrigger ? EColliderType::TRIGGER : EColliderType::COLLISION);
		}
	}
	NotifyPhysicsStateChange();
}

void RigidBodyComponent::SetColliderEnabled(bool enabled)
{
	m_collisionEnabled = enabled;
	NotifyPhysicsStateChange();
}

void RigidBodyComponent::UseGravity(bool useGravity)
{
	m_useGravity = useGravity;
	NotifyPhysicsStateChange();
}

void RigidBodyComponent::NotifyPhysicsStateChange()
{
	PhysicsManager::RigidBodyState state;
	state.id = GetOwner()->GetInstanceID();
	state.isKinematic = m_setKinematic;
	state.isTrigger = m_setTrigger;
	state.isColliderEnabled = m_collisionEnabled;
	state.useGravity = m_useGravity;

	PhysicsManagers->SetRigidBodyState(state);
}
