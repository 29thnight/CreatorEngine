#pragma once
#include "Component.h"
#include "IAwakable.h"
#include "IOnDestroy.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"
#include "TerrainColliderComponent.generated.h"

class TerrainColliderComponent : public Component, public ICollider, public IAwakable, public IOnDestroy
{
public:
   ReflectTerrainColliderComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(TerrainColliderComponent)

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
	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	
	void SetColliderID(unsigned int id) {
		m_colliderID = id;
	}
	unsigned int GetColliderID() {
		return m_colliderID;
	}


	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override {
		m_posOffset = pos;
	}

	DirectX::SimpleMath::Vector3 GetPositionOffset() override {
		return m_posOffset;
	}


	HeightFieldColliderInfo GetHeightFieldColliderInfo() const
	{
		return m_heightFieldColliderInfo;
	}

	void SetHeightFieldColliderInfo(const HeightFieldColliderInfo& info)
	{
		m_heightFieldColliderInfo = info;
	};

private:
	unsigned int m_colliderID;
	HeightFieldColliderInfo m_heightFieldColliderInfo; // �ݶ��̴� ����

	
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	// ICollider��(��) ���� ��ӵ�
	//terrain collider�� rotation�� �ʿ���� 
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override {
		m_rotOffset = rotation;
	}

	DirectX::SimpleMath::Quaternion GetRotationOffset() override {
		return m_rotOffset;
	}


	//terrain collider�� trigger�� �ʿ���� ���� �浹������ ����Ʈ �߰��� �ʿ�������
	void OnTriggerEnter(ICollider* other) override;

	void OnTriggerStay(ICollider* other) override;

	void OnTriggerExit(ICollider* other) override;

	void OnCollisionEnter(ICollider* other) override;

	void OnCollisionStay(ICollider* other) override;

	void OnCollisionExit(ICollider* other) override;

};

