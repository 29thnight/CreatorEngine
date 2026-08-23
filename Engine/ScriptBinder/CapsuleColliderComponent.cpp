#include "CapsuleColliderComponent.h"


void CapsuleColliderComponent::OnTriggerEnter(ICollider* other)
{
	++m_collsionCount;
}

void CapsuleColliderComponent::OnTriggerStay(ICollider* other)
{
}

void CapsuleColliderComponent::OnTriggerExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}

void CapsuleColliderComponent::OnCollisionEnter(ICollider* other)
{
	++m_collsionCount;
}

void CapsuleColliderComponent::OnCollisionStay(ICollider* other)
{
}

void CapsuleColliderComponent::OnCollisionExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		--m_collsionCount;
	}
}
