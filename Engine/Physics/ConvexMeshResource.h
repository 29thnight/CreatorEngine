#pragma once
#include <physx/PxPhysicsAPI.h>
#include <mathematics/vector3.hpp>
#include "ResourceBase.h"

class ConvexMeshResource : public ResourceBase
{
public:
	ConvexMeshResource(physx::PxPhysics* physics, math::vector3* vertices, int vertexSize, int polygonLimit);
	virtual ~ConvexMeshResource();

	inline physx::PxConvexMesh* GetConvexMesh() const { return m_convexMesh; }

private:
	physx::PxConvexMesh* m_convexMesh;
};

