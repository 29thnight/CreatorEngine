#include "TriangleMeshResource.h"
#include "PhysicsMathAdapter.h"

#include <vector>

TriangleMeshResource::TriangleMeshResource(physx::PxPhysics* PxPhysics, const math::vector3* vertices, const unsigned int& vertexSize, const unsigned int* indices, const unsigned int& indexSize) :ResourceBase(EResourceType::TRIANGLE_MESH)
{
	std::vector<physx::PxVec3> pxVertices;
	pxVertices.reserve(vertexSize);
	for (unsigned int index = 0; index < vertexSize; ++index)
	{
		pxVertices.push_back(PhysicsMath::ToPx(vertices[index]));
	}

	physx::PxTolerancesScale scale;
	physx::PxCookingParams params(scale);

	//GPU에서 계산된 메쉬를 사용하기 위한 설정
	params.buildGPUData = true;
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;

	physx::PxTriangleMeshDesc meshDesc;
	meshDesc.points.count = vertexSize;
	meshDesc.points.stride = sizeof(physx::PxVec3);
	meshDesc.points.data = pxVertices.data();

	meshDesc.triangles.count = indexSize / 3;
	meshDesc.triangles.stride = sizeof(unsigned int) * 3;
	meshDesc.triangles.data = (void*)indices;

#ifdef _DEBUG
	bool res = PxValidateTriangleMesh(params, meshDesc);
	if (res == false)
	{
		Debug->LogError("TriangleMeshResource::TriangleMeshResource() : PxValidateTriangleMesh failed");
	}
#endif

	m_triangleMesh = PxCreateTriangleMesh(params, meshDesc, PxPhysics->getPhysicsInsertionCallback());

}

TriangleMeshResource::~TriangleMeshResource()
{
	if (m_triangleMesh != nullptr)
	{
		m_triangleMesh->release();
		m_triangleMesh = nullptr;
	}
}
