#pragma once
#include "../Utility_Framework/ClassProperty.h"
#include <physx/PxPhysicsAPI.h>
#include <physx/characterkinematic/PxController.h>
#include <physx/characterkinematic/PxCapsuleController.h>
#include <physx/characterkinematic/PxControllerManager.h>
#include <vector>
#include "PhysicsInfo.h"

class PhysicX : public Singleton<PhysicX>
{
private:
	friend class Singleton;

private:
	PhysicX() = default;
	~PhysicX() = default;
public:
	// IPhysicsSystem��(��) ���� ��ӵ�
	void Initialize();
	void UnInitialize();

	void PreUpdate();
	void Update(float fixedDeltaTime);
	void PostUpdate();
	void PostUpdate(float interpolated);

	void AddRigidBody();

	void ClearActors();

	void AddActor(physx::PxActor* actor) { m_scene->addActor(*actor); }
	void ConnectPVD();

	physx::PxPhysics* GetPhysics() { return m_physics; }
	physx::PxScene* GetPxScene() { return m_scene; }
	physx::PxMaterial* GetDefaultMaterial() { return m_defaultMaterial; }
	physx::PxControllerManager* GetControllerManager() { return m_controllerManager; }
private:
	physx::PxDefaultAllocator		m_allocator;
	physx::PxDefaultErrorCallback	m_errorCallback;

	physx::PxPvd* pvd = nullptr;
	physx::PxFoundation* m_foundation = nullptr;
	physx::PxPhysics* m_physics = nullptr;
	physx::PxScene* m_scene = nullptr;

	physx::PxDefaultCpuDispatcher* gDispatcher = nullptr;

	physx::PxMaterial* m_defaultMaterial = nullptr;

	physx::PxControllerManager* m_controllerManager = nullptr;
public:
	inline physx::PxVec3 Lerp(const physx::PxVec3& a, const physx::PxVec3& b, float t) { return a + t * (b - a); }
	inline physx::PxQuat Slerp(const physx::PxQuat& q1, const physx::PxQuat& q2, float t) {
		// ��꿡 ����� �� ���ʹϾ�
		physx::PxQuat q2Adjusted = q2;
		float dot = q1.dot(q2);

		// �� ���ʹϾ��� �ݴ� ������ ����Ű�� ����
		if (dot < 0.0f) {
			dot = -dot;
			q2Adjusted = -q2;
		}

		// �Ӱ谪 ���� (���� �������� ��ü�� ����)
		const float threshold = 0.9995f;

		if (dot > threshold) {
			// ���ʹϾ��� ���� ��ġ -> ���� ����
			physx::PxQuat result = q1 + (q2Adjusted - q1) * t;
			result.normalize();
			return result;
		}

		// ���� ���
		float theta = acos(dot);
		float sinTheta = sqrt(1.0f - dot * dot);

		// ���� ��� ���
		float a = sin((1.0f - t) * theta) / sinTheta;
		float b = sin(t * theta) / sinTheta;

		// ������ ���ʹϾ� ��ȯ
		return (q1 * a) + (q2Adjusted * b);
	}
public:
	bool recordMemoryAllocations = true; // �޸� �������ϸ� ���࿩��.
	void ShowNotRelease();

	void GetShapes(std::vector<BoxShape>& out);
};

inline static auto Physics = PhysicX::GetInstance();
