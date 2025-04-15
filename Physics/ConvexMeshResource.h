#pragma once
#include <physx/PxPhysicsAPI.h>
#include "../Utility_Framework/Core.Minimal.h"
#include "ResourceBase.h"

class ConvexMeshResource : public ResourceBase
{
public:
	ConvexMeshResource(physx::PxPhysics* physics,Mathf::Vector3* vertices,int vertexSize,int polygonLimit);
	virtual ~ConvexMeshResource();

	inline physx::PxConvexMesh* GetConvexMesh() const { return m_convexMesh; }

private:
	physx::PxConvexMesh* m_convexMesh;
};

