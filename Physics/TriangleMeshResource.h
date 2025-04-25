#pragma once
#include <physx/PxPhysicsAPI.h>
#include <directxtk/SimpleMath.h>
#include "../Utility_Framework/LogSystem.h"
#include "ResourceBase.h"

class TriangleMeshResource : public ResourceBase
{
public:
	TriangleMeshResource(physx::PxPhysics* PxPhysics, const DirectX::SimpleMath::Vector3* vertices, const unsigned int& vertexSize, const unsigned int* indices, const unsigned int& indexSize);
	virtual ~TriangleMeshResource();

	inline physx::PxTriangleMesh* GetTriangleMesh() const { return m_triangleMesh; }
private:
	physx::PxTriangleMesh* m_triangleMesh;

	//physx::PxDeformableVolumeMesh* m_deformableVolumeMesh;
};

