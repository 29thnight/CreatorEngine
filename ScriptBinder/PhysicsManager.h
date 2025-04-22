#pragma once
#include <directxtk/SimpleMath.h>
#include <physx/PxPhysicsAPI.h>

class PhysicsManager
{
public:
	PhysicsManager();
	~PhysicsManager();
	void Initialize();
	void Finalize();
	void Update(float deltaTime);
	void SetGravity(const DirectX::SimpleMath::Vector3& gravity);
	void SetSimulationSpeed(float speed);
	inline physx::PxScene* GetScene() { return m_scene; }
	inline physx::PxPhysics* GetPhysics() { return m_physics; }
	inline physx::PxFoundation* GetFoundation() { return m_foundation; }

private:

	physx::PxFoundation* m_foundation;
	physx::PxPhysics* m_physics;
	physx::PxScene* m_scene;
	physx::PxDefaultCpuDispatcher* m_dispatcher;
	physx::PxCudaContextManager* m_cudaContextManager;
	physx::PxCudaContext* m_cudaContext;
	float m_simulationSpeed;
	DirectX::SimpleMath::Vector3 m_gravity;

};

