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

void RigidBodyComponent::SetImpulseForce(const Mathf::Vector3& force)
{
	shouldApplyImpulse = true;
	impulse = force;
}
