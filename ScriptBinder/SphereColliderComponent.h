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

	[[Property]]
	SphereColliderInfo m_Info;
	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	//info
	float GetRadius() const
	{
		return m_Info.radius;
	}
	void SetRadius(float radius)
	{
		m_Info.radius = radius;
	}
	EColliderType GetColliderType() const
	{
		return m_type;
	}
	void SetColliderType(EColliderType type)
	{
		m_type = type;
	}
	SphereColliderInfo GetSphereInfo() const
	{
		return m_Info;
	}
	void SetSphereInfoMation(const SphereColliderInfo& info)
	{
		m_Info = info;
	}
private:
	EColliderType m_type;
	unsigned int m_collsionCount = 0;
	// ICollider을(를) 통해 상속됨
	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override {}
	DirectX::SimpleMath::Vector3 GetPositionOffset() override { return m_posOffset; }
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override {}
	DirectX::SimpleMath::Quaternion GetRotationOffset() override { return m_rotOffset; }
	void OnTriggerEnter(ICollider* other) override {}
	void OnTriggerStay(ICollider* other) override {}
	void OnTriggerExit(ICollider* other) override {}
	void OnCollisionEnter(ICollider* other) override {}
	void OnCollisionStay(ICollider* other) override {}
	void OnCollisionExit(ICollider* other) override {}
};
