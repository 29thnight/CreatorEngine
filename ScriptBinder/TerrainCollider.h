#pragma once
#include "Component.h"
#include "../physics/PhysicsCommon.h"
#include "../Physics/ICollider.h"

class TerrainCollider : public Component, public ICollider
{
public:
	TerrainCollider();
	~TerrainCollider();

	
	void SetColliderID(unsigned int id) {
		m_colliderID = id;
	}
	unsigned int GetColliderID() {
		return m_colliderID;
	}


	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;

	DirectX::SimpleMath::Vector3 GetPositionOffset() override;


private:
	unsigned int m_colliderID;

	DirectX::SimpleMath::Vector3 m_posOffset{ 0.0f, 0.0f, 0.0f };
	DirectX::SimpleMath::Quaternion m_rotOffset{ 0.0f, 0.0f, 0.0f, 1.0f };

	// ICollider을(를) 통해 상속됨
	//terrain collider는 rotation이 필요없음 
	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;

	DirectX::SimpleMath::Quaternion GetRotationOffset() override;


	//terrain collider는 trigger가 필요없음 차후 충돌지점에 이팩트 추가시 필요할지도
	void OnTriggerEnter(ICollider* other) override;

	void OnTriggerStay(ICollider* other) override;

	void OnTriggerExit(ICollider* other) override;

	void OnCollisionEnter(ICollider* other) override;

	void OnCollisionStay(ICollider* other) override;

	void OnCollisionExit(ICollider* other) override;

};

