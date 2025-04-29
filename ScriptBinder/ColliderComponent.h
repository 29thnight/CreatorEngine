#pragma once

#include "Component.h"
#include "IColliderComponent.h"
#include "ICollider.h"
#include "Physx.h"

class ColliderComponent : public Component, public ICollider
{
public:
	DirectX::SimpleMath::Vector3 GetOffsetTransform() const { return m_offsetTransform; }
	void SetOffsetTransform(const DirectX::SimpleMath::Vector3& offset) { m_offsetTransform = offset; }
	DirectX::SimpleMath::Vector3 GetOffsetRotation() const { return m_offsetRotation; }
	void SetOffsetRotation(const DirectX::SimpleMath::Vector3& offset) { m_offsetRotation = offset; }

	
private:
	DirectX::SimpleMath::Vector3 m_offsetTransform{ 0.0f, 0.0f, 0.0f };
	DirectX::SimpleMath::Vector3 m_offsetRotation{ 0.0f, 0.0f, 0.0f };



	// ICollider을(를) 통해 상속됨
	void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) override;

	DirectX::SimpleMath::Vector3 GetPositionOffset() override;

	void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) override;

	DirectX::SimpleMath::Quaternion GetRotationOffset() override;

	void SetIsTrigger(bool isTrigger) override;

	bool GetIsTrigger() override;

	void OnTriggerEnter(ICollider* other) override;

	void OnTriggerStay(ICollider* other) override;

	void OnTriggerExit(ICollider* other) override;

	void OnCollisionEnter(ICollider* other) override;

	void OnCollisionStay(ICollider* other) override;

	void OnCollisionExit(ICollider* other) override;

};
