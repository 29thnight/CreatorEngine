#pragma once
#include "Component.h"
#include "IAwakable.h"
#include "IOnDestroy.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "MeshColliderComponent.generated.h"

class MeshColliderComponent : public Component, public ICollider, public IAwakable, public IOnDestroy
{
public:
   ReflectMeshColliderComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(MeshColliderComponent)
	[[Property]]
	ConvexMeshColliderInfo m_Info; 
	[[Property]]
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	[[Property]]
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };
	
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

	void OnTriggerEnter(ICollider* other) override {
		++m_collsionCount;
	}
	void OnTriggerStay(ICollider* other) override {
	}
	void OnTriggerExit(ICollider* other) override {
		if (m_collsionCount != 0) {
			--m_collsionCount;
		}
	}
	void OnCollisionEnter(ICollider* other) override {
		++m_collsionCount;
	}
	void OnCollisionStay(ICollider* other) override {

	}
	void OnCollisionExit(ICollider* other) override {
		if (m_collsionCount != 0) {
			--m_collsionCount;
		}
	}
	void SetColliderType(EColliderType type) override {
		m_type = type;
	}
	EColliderType GetColliderType() const override {
		return m_type;
	}
private:
	EColliderType m_type;
	unsigned int m_collsionCount = 0;
	
};
