#pragma once
#include <tuple>
#include "PhysicsInfo.h";

struct IRigidbody
{
	// 객체의 월드 위치를 가져옵니다.
	virtual std::tuple<float, float, float> GetWorldPosition() = 0;
	// 객체의 월드 회전을 가져옵니다.
	virtual std::tuple<float, float, float, float> GetWorldRotation() = 0;

	// 부모의 역행렬을 받아와서 local로 변환하여 적용시켜야합니다. 없다면 바로 적용.
	virtual void SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot) = 0;

	// 힘을 가할 때 사용.
	virtual void AddForce(float* velocity, ForceMode mode) = 0;
	virtual void AddTorque(float* velocity, ForceMode mode) = 0;

	// 설정한 rigidbody 정보를 전달합니다.
	inline virtual RigidbodyInfo* GetInfo() = 0;
};

