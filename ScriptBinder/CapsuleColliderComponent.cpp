#include "CapsuleColliderComponent.h"

CapsuleColliderComponent::CapsuleColliderComponent()
	: m_Info{}, m_type(EColliderType::COLLISION)
{
	m_Info.radius = 1.0f;
	m_Info.height = 1.0f;
}

CapsuleColliderComponent::~CapsuleColliderComponent()
{
}

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
