#include "BoxColliderComponent.h"

void BoxColliderComponent::SetPositionOffset(DirectX::SimpleMath::Vector3 pos)
{
	m_posOffset = pos;
}

DirectX::SimpleMath::Vector3 BoxColliderComponent::GetPositionOffset()
{
	return m_posOffset;
}

void BoxColliderComponent::SetRotationOffset(DirectX::SimpleMath::Quaternion rotation)
{
	m_rotOffset = rotation;
}

DirectX::SimpleMath::Quaternion BoxColliderComponent::GetRotationOffset()
{
	return m_rotOffset;
}


void BoxColliderComponent::OnTriggerEnter(ICollider* other)
{
	++m_collsionCount;
}

void BoxColliderComponent::OnTriggerStay(ICollider* other)
{
}

void BoxColliderComponent::OnTriggerExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}

void BoxColliderComponent::OnCollisionEnter(ICollider* other)
{
	++m_collsionCount;
}

void BoxColliderComponent::OnCollisionStay(ICollider* other)
{
}

void BoxColliderComponent::OnCollisionExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}
