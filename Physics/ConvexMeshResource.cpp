#include "ConvexMeshResource.h"

ConvexMeshResource::ConvexMeshResource(physx::PxPhysics* physics, Mathf::Vector3* vertices, int vertexSize, int polygonLimit) : ResourceBase(EResourceType::CONVRX_MESH)
{
	//컨벡스 매쉬 생성
	physx::PxConvexMeshDesc convexDesc;
	convexDesc.points.count = vertexSize;
	convexDesc.points.stride = sizeof(physx::PxVec3);
	convexDesc.vertexLimit = 255;
	convexDesc.points.data = (void*)vertices;
	convexDesc.polygonLimit = polygonLimit;
	convexDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

	physx::PxTolerancesScale scale;
	physx::PxCookingParams params(scale);
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_CLEAN_MESH;
	params.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eDISABLE_ACTIVE_EDGES_PRECOMPUTE;

	physx::PxDefaultMemoryOutputStream writeBuffer;
	physx::PxConvexMeshCookingResult::Enum result;

	PxCookConvexMesh(params, convexDesc, writeBuffer, &result);
	delete[] vertices; //할당된 버텍스 메모리 해제

	physx::PxDefaultMemoryInputData Input(writeBuffer.getData(), writeBuffer.getSize());
	m_convexMesh = physics->createConvexMesh(Input);
}

ConvexMeshResource::~ConvexMeshResource()
{
	if (m_convexMesh != nullptr)
	{
		m_convexMesh->release();
		m_convexMesh = nullptr;
	}
}
