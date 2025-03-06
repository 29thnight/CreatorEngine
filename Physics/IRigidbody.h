#pragma once
#include <tuple>
#include "PhysicsInfo.h";

struct IRigidbody
{
	// ��ü�� ���� ��ġ�� �����ɴϴ�.
	virtual std::tuple<float, float, float> GetWorldPosition() = 0;
	// ��ü�� ���� ȸ���� �����ɴϴ�.
	virtual std::tuple<float, float, float, float> GetWorldRotation() = 0;

	// �θ��� ������� �޾ƿͼ� local�� ��ȯ�Ͽ� ������Ѿ��մϴ�. ���ٸ� �ٷ� ����.
	virtual void SetGlobalPosAndRot(std::tuple<float, float, float> pos, std::tuple<float, float, float, float> rot) = 0;

	// ���� ���� �� ���.
	virtual void AddForce(float* velocity, ForceMode mode) = 0;
	virtual void AddTorque(float* velocity, ForceMode mode) = 0;

	// ������ rigidbody ������ �����մϴ�.
	inline virtual RigidbodyInfo* GetInfo() = 0;
};

