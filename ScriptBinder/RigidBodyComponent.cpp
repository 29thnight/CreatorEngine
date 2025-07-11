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

void RigidBodyComponent::OnDistroy()
{
	auto scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		scene->UnCollectRigidBodyComponent(this);
	}
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
