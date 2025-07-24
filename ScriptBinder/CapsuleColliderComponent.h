#pragma once
#include "Component.h"
#include "IRegistableEvent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "CapsuleColliderComponent.generated.h"

class CapsuleColliderComponent : public Component, public ICollider, public RegistableEvent<CapsuleColliderComponent>
{
public:
   ReflectCapsuleColliderComponent
	[[Serializable(Inheritance:Component)]]
	CapsuleColliderComponent() 
   {
		m_name = "CapsuleColliderComponent"; 
		m_typeID = TypeTrait::GUIDCreator::GetTypeID<CapsuleColliderComponent>();
		m_type = EColliderType::COLLISION;
		m_Info.radius = 1.0f;
		m_Info.height = 1.0f;
	} 
   virtual ~CapsuleColliderComponent() = default;
	
   void Awake() override
   {
	   auto scene = SceneManagers->GetActiveScene();
	   if (scene)
	   {
		   scene->CollectColliderComponent(this);
	   }
   }

   void OnDestroy() override
   {
	   auto scene = SceneManagers->GetActiveScene();
	   if (scene)
	   {
		   scene->UnCollectColliderComponent(this);
	   }
   }
	

	[[Property]]
	float m_radius{ 1.0f };
	[[Property]]
	float m_height{ 1.0f };
	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	//info
	float GetRadius()
	{
		if (m_radius != 0.0f)
		{
			m_Info.radius = m_radius;
		}
		return m_Info.radius;
	}
	void SetRadius(float radius)
	{
		m_Info.radius = radius;
		m_radius = m_Info.radius;
	}
	float GetHeight()
	{
		if (m_height != 0.0f)
		{
			m_Info.height = m_height;
		}
		return m_Info.height;
	}
	void SetHeight(float height)
	{
		m_Info.height = height;
		m_height = m_Info.height;
	}


	/*EColliderType GetColliderType() const
	{
		return m_type;
	}
	void SetColliderType(EColliderType type)
	{
		m_type = type;
	}*/
	CapsuleColliderInfo GetCapsuleInfo()
	{
		if (m_radius != 0.0f)
		{
			m_Info.radius = m_radius;
		}
		if (m_height != 0.0f)
		{
			m_Info.height = m_height;
		}
		return m_Info;
	}
	void SetCapsuleInfoMation(const CapsuleColliderInfo& info)
	{
		m_Info = info;
		m_radius = m_Info.radius;
		m_height = m_Info.height;
	}

	float GetStaticFriction() const
	{
		return m_Info.colliderInfo.staticFriction;
	}
	void SetStaticFriction(float staticFriction)
	{
		m_Info.colliderInfo.staticFriction = staticFriction;
	}
	float GetDynamicFriction() const
	{
		return m_Info.colliderInfo.dynamicFriction;
	}
	void SetDynamicFriction(float dynamicFriction)
	{
		m_Info.colliderInfo.dynamicFriction = dynamicFriction;
	}
	float GetRestitution() const
	{
		return m_Info.colliderInfo.restitution;
	}
	void SetRestitution(float restitution)
	{
		m_Info.colliderInfo.restitution = restitution;
	}
	float GetDensity() const
	{
		return m_Info.colliderInfo.density;
	}
	void SetDensity(float density)
	{
		m_Info.colliderInfo.density = density;
	}


	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override {
		m_posOffset = pos;
	}
	DirectX::SimpleMath::Vector3 GetPositionOffset() override {
		return m_posOffset;
	}
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override {
		m_rotOffset = rotation;
	}
	DirectX::SimpleMath::Quaternion GetRotationOffset() override {
		return m_rotOffset;
	}

	/*void SetIsTrigger(bool isTrigger) override;
	bool GetIsTrigger() override;*/

	//콜리전 이벤트
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;
private:
	//EColliderType m_type;
	int m_collsionCount{ 0 };
	CapsuleColliderInfo m_Info;
};
