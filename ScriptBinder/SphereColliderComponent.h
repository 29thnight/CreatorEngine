#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "SphereColliderComponent.generated.h"

class SphereColliderComponent : public Component, public ICollider
{
public:
   ReflectSphereColliderComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(SphereColliderComponent)

   void Awake() override
   {
	   auto scene = GetOwner()->m_ownerScene;
	   if (scene)
	   {
		   scene->CollectColliderComponent(this);
	   }
   }

   void OnDestroy() override
   {
	   auto scene = GetOwner()->m_ownerScene;
	   if (scene)
	   {
		   scene->UnCollectColliderComponent(this);
	   }
   }

	[[Property]]
	float radius = 1.0f;
	[[Property]]
	float staticFriction = 0.5f;	//정적 물체 마찰 계수
	[[Property]]
	float dynamicFriction = 0.4f;	//동적 물체 마찰 계수
	[[Property]]
	float restitution = 0.3f;	//탄성 계수
	[[Property]]
	float density = 10.0f;	//밀도


	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	//info
	float GetRadius()
	{
		if (radius != 0.0f)
		{
			m_Info.radius = radius;
		}
		return m_Info.radius;
	}
	void SetRadius(float radius)
	{
		m_Info.radius = radius;
		this->radius = m_Info.radius;
	}

	//collider info
	float GetStaticFriction()
	{
		if (staticFriction != 0.0f)
		{
			m_Info.colliderInfo.staticFriction = staticFriction;
		}
		return m_Info.colliderInfo.staticFriction;
	}

	void SetStaticFriction(float staticFriction)
	{
		m_Info.colliderInfo.staticFriction = staticFriction;
		this->staticFriction = m_Info.colliderInfo.staticFriction;
	}

	float GetDynamicFriction()
	{
		if (dynamicFriction != 0.0f)
		{
			m_Info.colliderInfo.dynamicFriction = dynamicFriction;
		}
		return m_Info.colliderInfo.dynamicFriction;
	}

	void SetDynamicFriction(float dynamicFriction)
	{
		m_Info.colliderInfo.dynamicFriction = dynamicFriction;
		this->dynamicFriction = m_Info.colliderInfo.dynamicFriction;
	}

	float GetRestitution()
	{
		if (restitution != 0.0f)
		{
			m_Info.colliderInfo.restitution = restitution;
		}
		return m_Info.colliderInfo.restitution;
	}
	void SetRestitution(float restitution)
	{
		m_Info.colliderInfo.restitution = restitution;
		this->restitution = m_Info.colliderInfo.restitution;
	}

	float GetDensity()
	{
		if (density != 0.0f)
		{
			m_Info.colliderInfo.density = density;
		}
		return m_Info.colliderInfo.density;
	}

	void SetDensity(float density)
	{
		m_Info.colliderInfo.density = density;
		this->density = m_Info.colliderInfo.density;
	}


	EColliderType GetColliderType() const
	{
		return m_type;
	}
	void SetColliderType(EColliderType type)
	{
		m_type = type;
	}

	SphereColliderInfo GetSphereInfo()
	{
		if (radius != 0.0f)
		{
			m_Info.radius = radius;
		}

		m_Info.colliderInfo.layerNumber = GetOwner()->m_collisionType;

		m_Info.colliderInfo.staticFriction = staticFriction;
		m_Info.colliderInfo.dynamicFriction = dynamicFriction;
		m_Info.colliderInfo.restitution = restitution;
		m_Info.colliderInfo.density = density;

		return m_Info;
	}
	void SetSphereInfoMation(const SphereColliderInfo& info)
	{
		m_Info = info;
		radius = m_Info.radius;
	}

	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override { m_posOffset = pos; }
	DirectX::SimpleMath::Vector3 GetPositionOffset() override { return m_posOffset; }
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override { m_rotOffset = rotation; }
	DirectX::SimpleMath::Quaternion GetRotationOffset() override { return m_rotOffset; }

private:
	SphereColliderInfo m_Info;
	EColliderType m_type;
	unsigned int m_collsionCount = 0;
	// ICollider을(를) 통해 상속됨
	void OnTriggerEnter(ICollider* other) override { ++m_collsionCount; }
	void OnTriggerStay(ICollider* other) override {}
	void OnTriggerExit(ICollider* other) override { if (m_collsionCount != 0) {--m_collsionCount;} }
	void OnCollisionEnter(ICollider* other) override { ++m_collsionCount; }
	void OnCollisionStay(ICollider* other) override {}
	void OnCollisionExit(ICollider* other) override { if (m_collsionCount != 0) { --m_collsionCount; } }
};
