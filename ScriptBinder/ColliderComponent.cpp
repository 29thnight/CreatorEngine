#include "ColliderComponent.h"

void ColliderComponent::SetPositionOffset(DirectX::SimpleMath::Vector3 pos)
{
}

DirectX::SimpleMath::Vector3 ColliderComponent::GetPositionOffset()
{
    return DirectX::SimpleMath::Vector3();
}

void ColliderComponent::SetRotationOffset(DirectX::SimpleMath::Quaternion rotation)
{
}

DirectX::SimpleMath::Quaternion ColliderComponent::GetRotationOffset()
{
    return DirectX::SimpleMath::Quaternion();
}

void ColliderComponent::SetIsTrigger(bool isTrigger)
{
}

bool ColliderComponent::GetIsTrigger()
{
    return false;
}

void ColliderComponent::OnTriggerEnter(ICollider* other)
{
}

void ColliderComponent::OnTriggerStay(ICollider* other)
{
}

void ColliderComponent::OnTriggerExit(ICollider* other)
{
}

void ColliderComponent::OnCollisionEnter(ICollider* other)
{
}

void ColliderComponent::OnCollisionStay(ICollider* other)
{
}

void ColliderComponent::OnCollisionExit(ICollider* other)
{
}
