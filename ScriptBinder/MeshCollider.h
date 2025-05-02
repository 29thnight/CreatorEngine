#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"

class MeshColliderComponent : public Component, public ICollider
{
public:
	MeshColliderComponent();
	~MeshColliderComponent() override;
	[[Property]]
	ConvexMeshColliderInfo m_Info;
	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	
	
	//info
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
	EColliderType GetColliderType() const
	{
		return m_type;
	}
	void SetColliderType(EColliderType type)
	{
		m_type = type;
	}

	unsigned int GetCollisionCount() const
	{
		return m_collsionCount;
	}



	//ConvexMesh
	unsigned char GetMeshPolygonLimit() const
	{
		return m_Info.convexPolygonLimit;
	}

	void SetMeshPolygonLimit(unsigned char polygonLimit)
	{
		m_Info.convexPolygonLimit = polygonLimit;
	}

	ConvexMeshColliderInfo GetMeshInfo() const
	{
		return m_Info;
	}
	void SetMeshInfoMation(const ConvexMeshColliderInfo& info)
	{
		m_Info = info;
	}




	//=========================================================
	// ICollider을(를) 통해 상속됨
	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;
	DirectX::SimpleMath::Vector3 GetPositionOffset() override;
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;
	DirectX::SimpleMath::Quaternion GetRotationOffset() override;
	void OnTriggerEnter(ICollider* other) override;
	void OnTriggerStay(ICollider* other) override;
		
	void OnTriggerExit(ICollider* other) override;
	void OnCollisionEnter(ICollider* other) override;
	void OnCollisionStay(ICollider* other) override;
	void OnCollisionExit(ICollider* other) override;
private:
	EColliderType m_type;
	unsigned int m_collsionCount = 0;
	
};