#pragma once
#include "ColliderComponent.h"
#include "../physics/PhysicsCommon.h"

class BoxColliderComponent : public ColliderComponent
{
public:
	[[Property]] 
	BoxColliderInfo m_Info;
	[[Property]] 
	bool m_IsTrigger = false;


	EColliderType GetColliderType() const
	{
		return m_IsTrigger ? EColliderType::TRIGGER : EColliderType::COLLISION;
	}

	void SetBoxInfoMation(const BoxColliderInfo& info)
	{
		m_Info = info;
	}
};
