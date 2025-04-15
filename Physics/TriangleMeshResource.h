#pragma once
#include <physx/PxPhysicsAPI.h>
#include "../Utility_Framework/Core.Minimal.h"
#include "ResourceBase.h"

class TriangleMeshResource : public ResourceBase
{
public:
	TriangleMeshResource(physx::PxPhysics* PxPhysics, const Mathf::Vector3* vertices, const unsigned int& vertexSize, const unsigned int* indices, const unsigned int& indexSize);
	virtual ~TriangleMeshResource();

	inline physx::PxTriangleMesh* GetTriangleMesh() const { return m_triangleMesh; }
private:
	physx::PxTriangleMesh* m_triangleMesh;

	//physx::PxDeformableVolumeMesh* m_deformableVolumeMesh;
};

