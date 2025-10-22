#pragma once
#include "Component.h"
#include "IRegistableEvent.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "SphereColliderComponent.generated.h"

class SphereColliderComponent : public Component, public ICollider, public RegistableEvent<SphereColliderComponent>
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
