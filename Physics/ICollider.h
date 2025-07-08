#pragma once
#include <tuple>

struct ICollider
{
	/*virtual void SetLocalPosition(std::tuple<float, float, float> pos) = 0;
	virtual void SetRotation(std::tuple<float, float, float, float> rotation) = 0;*/
	
	//position offset
	virtual void SetPositionOffset(DirectX::SimpleMath::Vector3 pos) = 0;
	virtual DirectX::SimpleMath::Vector3 GetPositionOffset() = 0;
	
	//rotation offset
	virtual void SetRotationOffset(DirectX::SimpleMath::Quaternion rotation) = 0;
	virtual DirectX::SimpleMath::Quaternion GetRotationOffset() = 0;


	////콜리전 활성화 여부
	//virtual void SetIsTrigger(bool isTrigger) = 0; 
	//virtual bool GetIsTrigger() = 0;

	//
	virtual void OnTriggerEnter(ICollider* other) = 0;
	virtual void OnTriggerStay(ICollider* other) = 0;
	virtual void OnTriggerExit(ICollider* other) = 0;

	virtual void OnCollisionEnter(ICollider* other) = 0;
	virtual void OnCollisionStay(ICollider* other) = 0;
	virtual void OnCollisionExit(ICollider* other) = 0;
};

