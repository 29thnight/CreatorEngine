#pragma once
#include <tuple>
#include "PhysicsCommon.h" // EColliderType을 위해 추가
struct ICollider
{
	virtual ~ICollider() = default;
	/*virtual void SetLocalPosition(std::tuple<float, float, float> pos) = 0;
	virtual void SetRotation(std::tuple<float, float, float, float> rotation) = 0;*/
	
	//position offset
	virtual void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) = 0;
	virtual DirectX::SimpleMath::Vector3 GetPositionOffset() = 0;
	
	//rotation offset
	virtual void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) = 0;
	virtual DirectX::SimpleMath::Quaternion GetRotationOffset() = 0;

	// 콜라이더 타입을 설정하고 가져오는 순수 가상 함수
	virtual void SetColliderType(EColliderType type) = 0;
	virtual EColliderType GetColliderType() const = 0;
	virtual void OnTriggerEnter(ICollider* other) = 0;
	virtual void OnTriggerStay(ICollider* other) = 0;
	virtual void OnTriggerExit(ICollider* other) = 0;

	virtual void OnCollisionEnter(ICollider* other) = 0;
	virtual void OnCollisionStay(ICollider* other) = 0;
	virtual void OnCollisionExit(ICollider* other) = 0;
};

