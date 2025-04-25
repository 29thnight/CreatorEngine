#pragma once

#include "Component.h"
#include "IColliderComponent.h"
#include "../physics/Physx.h"

class ColliderComponent : public Component, public IColliderComponent
{
public:
	DirectX::SimpleMath::Vector3 GetOffsetTransform() const { return m_offsetTransform; }
	void SetOffsetTransform(const DirectX::SimpleMath::Vector3& offset) { m_offsetTransform = offset; }
	DirectX::SimpleMath::Vector3 GetOffsetRotation() const { return m_offsetRotation; }
	void SetOffsetRotation(const DirectX::SimpleMath::Vector3& offset) { m_offsetRotation = offset; }

	
private:
	DirectX::SimpleMath::Vector3 m_offsetTransform{ 0.0f, 0.0f, 0.0f };
	DirectX::SimpleMath::Vector3 m_offsetRotation{ 0.0f, 0.0f, 0.0f };

	// PhysicsEventCallback → 이 람다를 통해 콜백이 전달됨
	void Dispatch(const CollisionData& d, ECollisionEventType type)
	{
		switch (type)
		{
		case ECollisionEventType::ENTER_COLLISION:   OnCollisionEnter(d); break;
		case ECollisionEventType::ON_COLLISION:      OnCollisionStay(d);  break;
		case ECollisionEventType::END_COLLISION:     OnCollisionExit(d);  break;
		case ECollisionEventType::ENTER_OVERLAP:     OnTriggerEnter(d);   break;
		case ECollisionEventType::ON_OVERLAP:        OnTriggerStay(d);    break;
		case ECollisionEventType::END_OVERLAP:       OnTriggerExit(d);    break;
		}
	}
};
