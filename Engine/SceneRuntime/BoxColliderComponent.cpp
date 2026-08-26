#include "BoxColliderComponent.h"

void BoxColliderComponent::SetPositionOffset(math::vector3 pos)
{
	m_posOffset = pos;
}

math::vector3 BoxColliderComponent::GetPositionOffset()
{
	return m_posOffset;
}

void BoxColliderComponent::SetRotationOffset(math::quaternion rotation)
{
	m_rotOffset = rotation;
}

math::quaternion BoxColliderComponent::GetRotationOffset()
{
	return m_rotOffset;
}


void BoxColliderComponent::OnTriggerEnter(ICollider* other)
{
	std::cout << "OnTriggerEnter" << std::endl;
	++m_collsionCount;
}

void BoxColliderComponent::OnTriggerStay(ICollider* other)
{
}

void BoxColliderComponent::OnTriggerExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		std::cout << "OnTriggerExit" << std::endl;
		--m_collsionCount;
	}
}

void BoxColliderComponent::OnCollisionEnter(ICollider* other)
{
	std::cout << "OnCollisionEnter" << std::endl;
	++m_collsionCount;
}

void BoxColliderComponent::OnCollisionStay(ICollider* other)
{
}

void BoxColliderComponent::OnCollisionExit(ICollider* other)
{
	if (m_collsionCount != 0) {
		std::cout << "OnCollisionExit" << std::endl;
		--m_collsionCount;
	}
}
