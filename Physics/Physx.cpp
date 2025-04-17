#include "Physx.h"
#include "PhysicsHelper.h"
#include "IRigidbody.h"
#include "PhysicsInfo.h"
#include "PhysicsEventCallback.h"
#include "ConvexMeshResource.h"
#include "TriangleMeshResource.h"
#include "HeightFieldResource.h"
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <tuple>
#include <mutex>
#include <iostream>

#include "ICollider.h"
#include <cuda_runtime.h>



// block(default), player, enemy, interact

bool intersectionMatrix[3][3] = {
	{1, 1, 1},
	{1, 1, 1},
	{1, 1, 0},
};

PxFilterFlags CustomFilterShader(
	PxFilterObjectAttributes at0,
	PxFilterData fd0,
	PxFilterObjectAttributes at1,
	PxFilterData fd1,
	PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
	int group0 = fd0.word0;
	int group1 = fd1.word0;

	//if (!collisionMatrix[group0][group1]) {
	//	return PxFilterFlag::eSUPPRESS; 
	//}


	pairFlags = PxPairFlag::eCONTACT_DEFAULT;
	pairFlags |= PxPairFlag::eTRIGGER_DEFAULT;

	pairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
	pairFlags |= PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
	pairFlags |= PxPairFlag::eNOTIFY_TOUCH_LOST;
	pairFlags |= PxPairFlag::eNOTIFY_CONTACT_POINTS;

	return PxFilterFlag::eDEFAULT;
}

void PhysicX::Initialize()
{
	m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);     

#if _DEBUG
	// PVD 생성
	// PVD  _DEBUG가 아닐시 nullptr
	pvd = PxCreatePvd(*m_foundation);
	PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
#endif

	//m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(1.f, 40.f), recordMemoryAllocations, pvd);  
	m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(1.f, 10.f), true, pvd);  // 피직스 초기화

	// physics 사용 코어 수
	UINT MaxThread = 8;
	UINT core = std::thread::hardware_concurrency();
	// 엔진 부 스레드 및 랜더링 스레드 고려 core 수를 조정
	if (core < 4) core = 2;
	else if (core > MaxThread + 4) core = MaxThread;
	else core -= 4;
	// 디스페처 설정
	gDispatcher = PxDefaultCpuDispatcherCreate(core);

	m_defaultMaterial = m_physics->createMaterial(1.f, 1.f, 0.f);

	//gDispatcher = PxDefaultCpuDispatcherCreate(2);  
	// CUDA 초기화
	PxCudaContextManagerDesc cudaDesc;
	m_cudaContextManager = PxCreateCudaContextManager(*m_foundation, cudaDesc);
	if (!m_cudaContextManager || !m_cudaContextManager->contextIsValid())
	{
		m_cudaContextManager->release();
		m_cudaContextManager = nullptr;
	}

	if (m_cudaContextManager) {
		m_cudaContext = m_cudaContextManager->getCudaContext();
		if (m_cudaContext) {
			std::cout << "CUDA context created successfully." << std::endl;
		}
		else {
			std::cout << "Failed to create CUDA context." << std::endl;
		}
	}
	else {
		std::cout << "CUDA context manager creation failed." << std::endl;
	}




	// Scene 셍성
	physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);

	
	sceneDesc.cpuDispatcher = gDispatcher;
	//sceneDesc.filterShader = PxDefaultSimulationFilterShader;
	sceneDesc.filterShader = CustomFilterShader;

	sceneDesc.simulationEventCallback = &eventCallback;

	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS; // 활성 액터만 업데이트
	//sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	// GPU 가속 사용	시 GPU에서 물리 연산을 수행하도록 설정
	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS; // GPU 
	sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU; // GPU 
	sceneDesc.cudaContextManager = m_cudaContextManager;

	m_scene = m_physics->createScene(sceneDesc); //

	m_controllerManager = PxCreateControllerManager(*m_scene);
}
//void PhysicX::HasConvexMeshResource(const unsigned int& hash)
//{
//	
//}
void PhysicX::RemoveActors()
{
	for (auto removeActor : m_removeActorList)
	{
		m_scene->removeActor(*removeActor);
	}
	m_removeActorList.clear();
}
void PhysicX::UnInitialize() {
	if (m_foundation) m_foundation->release();
}








void PhysicX::Update(float fixedDeltaTime)
{

}

void PhysicX::FinalUpdate()
{
	//rigidbody
}

void PhysicX::SetCallBackCollisionFunction(std::function<void(CollisionData, EColliderType)> func)
{
	eventCallback.SetCallbackFunction(func);
}

void PhysicX::SetPhysicsInfo()
{
		
	// m_scene->setGravity(PxVec3(0.0f, -9.81f, 0.0f));
	//collisionMatrix[0][0] = true;
}

void PhysicX::ChangeScene()
{
		
	//DeformableSurface
		
}

RayCastOutput PhysicX::RayCast(const RayCastInput & in, bool isStatic = false)
{
	physx::PxVec3 pxOrgin;
	physx::PxVec3 pxDirection;
	CopyVectorDxToPx(in.origin, pxOrgin);
	CopyVectorDxToPx(in.direction, pxDirection);

	// RaycastHit 
	const physx::PxU32 maxHits = 20;
	physx::PxRaycastHit hitBuffer[maxHits];
	physx::PxRaycastBuffer hitBufferStruct(hitBuffer, maxHits);

	//충돌 쿼리 정보
	physx::PxQueryFilterData filterData;
	filterData.data.word0 = in.layerNumber;
	filterData.data.word1 = m_collisionMatrix[in.layerNumber];

	if (isStatic)
	{
		filterData.flags = physx::PxQueryFlag::eSTATIC
			| physx::PxQueryFlag::ePREFILTER
			| physx::PxQueryFlag::eNO_BLOCK
			| physx::PxQueryFlag::eDISABLE_HARDCODED_FILTER;
	}
	else
	{
		filterData.flags = physx::PxQueryFlag::eDYNAMIC
			| physx::PxQueryFlag::ePREFILTER
			| physx::PxQueryFlag::eNO_BLOCK
			| physx::PxQueryFlag::eDISABLE_HARDCODED_FILTER;
	}

	//RaycastQueryFilter queryFilter
	bool isAniHit;
		

	RayCastOutput out;
		

	return out;
}
//==========================================================================================
//rigid body

void PhysicX::CreateStaticBody(const BoxColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetExtent(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z);
	}
	
}

void PhysicX::CreateStaticBody(const SphereColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetRadius(info.radius);
	}
}

void PhysicX::CreateStaticBody(const CapsuleColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetRadius(info.radius);
		staticBody->SetHalfHeight(info.height);
	}
}

void PhysicX::CreateStaticBody(const ConvexMeshColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	ConvexMeshResource* convexMesh = new ConvexMeshResource(m_physics, info.vertices, info.vertexSize, info.convexPolygonLimit);
	physx::PxConvexMesh* pxConvexMesh = convexMesh->GetConvexMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);
	shape->release();
}

void PhysicX::CreateStaticBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices,info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);

	shape->release();	
}

void PhysicX::CreateStaticBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols,info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, collisionMatrix);
	staticBody->SetOffsetRotation(Mathf::Matrix::CreateRotationZ(180.0f / 180.0f * 3.14f));
	staticBody->SetOffsetTranslation(Mathf::Matrix::CreateTranslation(Mathf::Vector3(info.rowScale * info.numRows * 0.5f, 0.0f, -info.colScale * info.numCols * 0.5f)));

	shape->release();

}

void PhysicX::CreateDynamicBody(const BoxColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material);

	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);

	shape->release();

	if (dynamicBody != nullptr)
	{
		dynamicBody->SetExtent(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z);
	}
}

void PhysicX::CreateDynamicBody(const SphereColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);
	shape->release();
	if (dynamicBody != nullptr)
	{
		dynamicBody->SetRadius(info.radius);
	}
}

void PhysicX::CreateDynamicBody(const CapsuleColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);
	shape->release();
	if (dynamicBody != nullptr)
	{
		dynamicBody->SetRadius(info.radius);
		dynamicBody->SetHalfHeight(info.height);
	}
}

void PhysicX::CreateDynamicBody(const ConvexMeshColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	/*static int number = 0;
	number++;*/

	ConvexMeshResource* convexMesh = new ConvexMeshResource(m_physics, info.vertices, info.vertexSize, info.convexPolygonLimit);
	physx::PxConvexMesh* pxConvexMesh = convexMesh->GetConvexMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material);

	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices, info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material);

	shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE,true);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType, int* collisionMatrix, bool isKinematic)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols, info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, collisionMatrix, isKinematic);
	shape->release();
}

StaticRigidBody* PhysicX::SettingStaticBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, int* collisionMatrix)
{
	//filterData
	physx::PxFilterData filterData;
	filterData.word0 = colInfo.layerNumber;
	filterData.word1 = collisionMatrix[colInfo.layerNumber];
	shape->setSimulationFilterData(filterData);

	//collisionData
	StaticRigidBody* staticBody = new StaticRigidBody(collideType,colInfo.id,colInfo.layerNumber);
	CollisionData* collisionData = new CollisionData();

	//스테틱 바디 초기화-->rigidbody 생성 및 shape attach , collider 정보 등록, collisionData 정보 등록
	if (!staticBody->Initialize(colInfo,shape,m_physics,collisionData))
	{
		Debug->LogError("PhysicX::SettingStaticBody() : staticBody Initialize failed id :" + std::to_string(colInfo.id));
		return nullptr;
	}

	//충돌데이터 등록, 리지드 바디 등록
	m_collisionDataContainer.insert(std::make_pair(colInfo.id, collisionData));
	m_rigidBodyContainer.insert(std::make_pair(staticBody->GetID(), staticBody));

	m_updateActors.push_back(staticBody);

	return staticBody;
}

DynamicRigidBody* PhysicX::SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, int* collisionMatrix, bool isKinematic)
{
	//필터데이터
	physx::PxFilterData filterData;
	filterData.word0 = colInfo.layerNumber;
	filterData.word1 = collisionMatrix[colInfo.layerNumber];
	shape->setSimulationFilterData(filterData);

	//collisionData
	DynamicRigidBody* dynamicBody = new DynamicRigidBody(collideType, colInfo.id, colInfo.layerNumber);
	CollisionData* collisionData = new CollisionData();

	//다이나믹 바디 초기화-->rigidbody 생성 및 shape attach , collider 정보 등록, collisionData 정보 등록
	if (!dynamicBody->Initialize(colInfo, shape, m_physics, collisionData,isKinematic))
	{
		Debug->LogError("PhysicX::SettingDynamicBody() : dynamicBody Initialize failed id :" + std::to_string(colInfo.id));
		return nullptr;
	}

	//충돌데이터 등록, 리지드 바디 등록
	m_collisionDataContainer.insert(std::make_pair(colInfo.id, collisionData));
	m_rigidBodyContainer.insert(std::make_pair(dynamicBody->GetID(), dynamicBody));

	m_updateActors.push_back(dynamicBody);

	return dynamicBody;
}

bool TransformDifferent(const physx::PxTransform& first, physx::PxTransform& second, float positionTolerance, float rotationTolerance) {

	//compare position
	if (!(first.p).isFinite() || !(second.p).isFinite() || (first.p - second.p).magnitude() > positionTolerance) {
		if (!(first.q).isFinite() || !(second.q).isFinite() || (physx::PxAbs(first.q.w - second.q.w) > rotationTolerance) || 
			(physx::PxAbs(first.q.x - second.q.x)>rotationTolerance)||
			(physx::PxAbs(first.q.y - second.q.y) > rotationTolerance) || 
			(physx::PxAbs(first.q.z - second.q.z) > rotationTolerance))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
}


void PhysicX::GetRigidBodyData(unsigned int id,RigidBodyGetSetData& rigidBodyData)
{
	auto body = m_rigidBodyContainer.find(id)->second;
	
	//dynamicBody 의 경우
	DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
	if (dynamicBody)
	{
		physx::PxRigidDynamic* pxBody = dynamicBody->GetRigidDynamic();
		Mathf::Matrix dxMatrix;
		CopyMatrixPxToDx(pxBody->getGlobalPose(), dxMatrix);
		rigidBodyData.transform = Mathf::Matrix::CreateScale(dynamicBody->GetScale()) * dxMatrix * dynamicBody->GetOffsetTranslation();
		CopyVectorPxToDx(pxBody->getLinearVelocity(), rigidBodyData.linearVelocity);
		CopyVectorPxToDx(pxBody->getAngularVelocity(), rigidBodyData.angularVelocity);

		physx::PxRigidDynamicLockFlags flags = pxBody->getRigidDynamicLockFlags();

		rigidBodyData.isLockLinearX = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X);
		rigidBodyData.isLockLinearY = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y);
		rigidBodyData.isLockLinearZ = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z);
		rigidBodyData.isLockAngularX = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X);
		rigidBodyData.isLockAngularY = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
		rigidBodyData.isLockAngularZ = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);
	}
	//staticBody 의 경우
	StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
	if (staticBody)
	{
		physx::PxRigidStatic* pxBody = staticBody->GetRigidStatic();
		Mathf::Matrix dxMatrix;
		CopyMatrixPxToDx(pxBody->getGlobalPose(), dxMatrix);
		rigidBodyData.transform = Mathf::Matrix::CreateScale(staticBody->GetScale()) * staticBody->GetOffsetRotation() *dxMatrix * staticBody->GetOffsetTranslation();
	}
}

void PhysicX::SetRigidBodyData(const unsigned int& id, const RigidBodyGetSetData& rigidBodyData, int* collisionMatrix)
{
	//데이터를 설정할 리지드 바디가 등록되어 있는지 검사
	if (m_rigidBodyContainer.find(id) == m_rigidBodyContainer.end())
	{
		return;
	}

	auto body = m_rigidBodyContainer.find(id)->second;

	//dynamicBody 의 경우
	DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
	if (dynamicBody)
	{
		//받은 데이터로 pxBody의 transform과 속도와 각속도 설정
		Mathf::Matrix dxMatrix = rigidBodyData.transform;
		physx::PxTransform pxTransform;
		physx::PxVec3 pxLinearVelocity;
		physx::PxVec3 pxAngularVelocity;
		CopyVectorDxToPx(rigidBodyData.linearVelocity, pxLinearVelocity);
		CopyVectorDxToPx(rigidBodyData.angularVelocity, pxAngularVelocity);


		physx::PxRigidDynamic* pxBody = dynamicBody->GetRigidDynamic();
		//운동학 객체가 아닌경우 각	속도 선속도 설정
		if (!(pxBody->getRigidBodyFlags()&physx::PxRigidBodyFlag::eKINEMATIC))
		{
			pxBody->setLinearVelocity(pxLinearVelocity);
			pxBody->setAngularVelocity(pxAngularVelocity);
		}

		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, rigidBodyData.isLockAngularX);
		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, rigidBodyData.isLockAngularY);
		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, rigidBodyData.isLockAngularZ);
		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X, rigidBodyData.isLockLinearX);
		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, rigidBodyData.isLockLinearY);
		pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, rigidBodyData.isLockLinearZ);

		Mathf::Vector3 position;
		Mathf::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		Mathf::Quaternion rotation;
		dxMatrix.Decompose(scale, rotation, position);
		dxMatrix = Mathf::Matrix::CreateScale(1.0f) * Mathf::Matrix::CreateFromQuaternion(rotation) * Mathf::Matrix::CreateTranslation(position);

		CopyMatrixDxToPx(dxMatrix, pxTransform);
		pxBody->setGlobalPose(pxTransform);
		dynamicBody->ChangeLayerNumber(rigidBodyData.LayerNumber, collisionMatrix);

		if (scale.x>0.0f&&scale.y>0.0f&&scale.z>0.0f)
		{
			dynamicBody->SetConvertScale(scale, m_physics, collisionMatrix);
		}
		else {
			Debug->LogError("PhysicX::SetRigidBodyData() : scale is 0.0f id :" + std::to_string(id));
		}
	}
	//staticBody 의 경우
	StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
	if (staticBody)
	{
		physx::PxRigidStatic* pxBody = staticBody->GetRigidStatic();
		Mathf::Matrix dxMatrix = rigidBodyData.transform;
		physx::PxTransform pxPrevTransform = pxBody->getGlobalPose();
		physx::PxTransform pxCurrTransform;
		
		Mathf::Vector3 position;
		Mathf::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		Mathf::Quaternion rotation;

		dxMatrix.Decompose(scale, rotation, position);
		dxMatrix = Mathf::Matrix::CreateScale(1.0f) * 
			Mathf::Matrix::CreateFromQuaternion(rotation) * staticBody->GetOffsetRotation().Invert() *
			Mathf::Matrix::CreateTranslation(position) * staticBody->GetOffsetTranslation().Invert();

		CopyMatrixDxToPx(dxMatrix, pxCurrTransform);
	}
}

void PhysicX::RemoveRigidBody(const unsigned int& id, physx::PxScene* scene, std::vector<physx::PxActor*>& removeActorList)
{
	//등록되어 있는지 검사
	if (m_rigidBodyContainer.find(id)==m_rigidBodyContainer.end())
	{
		Debug->LogWarning("RemoveRigidBody id :" + std::to_string(id) + " Remove Failed");
		return;
	}

	RigidBody* body = m_rigidBodyContainer[id];

	//삭제할 리지드 바디 등록 (PxActor는 다음 프레임에 삭제)
	if (body)
	{
		DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
		if (dynamicBody) {
			if (dynamicBody->GetRigidDynamic()->getScene() == scene)
			{
				removeActorList.push_back(dynamicBody->GetRigidDynamic());
				m_rigidBodyContainer.erase(id);
			}
		}
		StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
		if (staticBody) {
			if (staticBody->GetRigidStatic()->getScene() == scene)
			{
				removeActorList.push_back(staticBody->GetRigidStatic());
				m_rigidBodyContainer.erase(id);
			}
		}
	}

	//등록 예정 지점도 확인후 삭제
	auto bodyIter = m_updateActors.begin();
	for (bodyIter; bodyIter!= m_updateActors.end(); bodyIter++)
	{
		if ((*bodyIter)->GetID()==id)
		{
			m_updateActors.erase(bodyIter);
			break;
		}
	}
	
}

void PhysicX::RemoveAllRigidBody(physx::PxScene* scene, std::vector<physx::PxActor*>& removeActorList)
{
	//삭제할 리지드 바디 등록 (물리씬에 등록된 상태면 다음 프레임에 삭제)
	for (const auto& body : m_rigidBodyContainer) {
		DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body.second);
		if (dynamicBody) {
			
			if (dynamicBody->GetRigidDynamic()->getScene() == scene)
			{
				removeActorList.push_back(dynamicBody->GetRigidDynamic());
				continue;
			}
		}
		StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body.second);
		if (staticBody) {
			if (staticBody->GetRigidStatic()->getScene() == scene)
			{
				removeActorList.push_back(staticBody->GetRigidStatic());
				continue;
			}
		}
	}
	//저장된 리지드 바디 삭제 
	m_rigidBodyContainer.clear();

	//등록예정인 리지드 바디 삭제
	for (const auto& body : m_updateActors) {
		DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
		if (dynamicBody) {
			if (dynamicBody->GetRigidDynamic()->getScene() == scene)
			{
				removeActorList.push_back(dynamicBody->GetRigidDynamic());
				continue;
			}
		}
		StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
		if (staticBody) {
			if (staticBody->GetRigidStatic()->getScene() == scene)
			{
				removeActorList.push_back(staticBody->GetRigidStatic());
				continue;
			}
		}
	}
	//등록된 리지드 바디 삭제
	m_updateActors.clear();
}

//==============================================
//케릭터 컨트롤러

void PhysicX::CreateCCT(const CharacterControllerInfo& controllerInfo, const CharacterMovementInfo& movementInfo)
{
	m_waittingCCTList.push_back(std::make_pair(controllerInfo, movementInfo));
}

void PhysicX::RemoveCCT(const unsigned int& id)
{
	auto controllerIter = m_characterControllerContainer.find(id);
	if (controllerIter!= m_characterControllerContainer.end()) {
		m_characterControllerContainer.erase(controllerIter);
	}
}

void PhysicX::RemoveAllCCT()
{
	m_characterControllerContainer.clear();
	m_updateCCTList.clear();
	m_waittingCCTList.clear();
}

void PhysicX::AddInputMove(const CharactorControllerInputInfo& info)
{
	if (m_characterControllerContainer.find(info.id) == m_characterControllerContainer.end())
	{
		return;
	}

	CharacterController* controller = m_characterControllerContainer[info.id];
	controller->AddMovementInput(info.input, info.isDynamic);
}

CharacterControllerGetSetData PhysicX::GetCCTData(const unsigned int& id)
{
	CharacterControllerGetSetData data;
	if (m_characterControllerContainer.find(id) == m_characterControllerContainer.end())
	{
		auto& controller = m_characterControllerContainer[id];
		physx::PxController* pxController = controller->GetController();

		controller->GetPosition(data.position);
	}
	else {
		for (auto& [controller, movement] : m_updateCCTList) {
			if (controller.id == id)
			{
				data.position = controller.position;
			}
		}

		for (auto& [controller, movement]:m_waittingCCTList)
		{
			if (controller.id == id)
			{
				data.position = controller.position;
			}
		}

	}	
	return data;
}

CharacterMovementGetSetData PhysicX::GetMovementData(const unsigned int& id)
{
	CharacterMovementGetSetData data;
	if (m_characterControllerContainer.find(id) == m_characterControllerContainer.end())
	{
		auto& controller = m_characterControllerContainer[id];
		CharacterMovement* movement = controller->GetCharacterMovement();
		
		data.velocity = movement->GetOutVector();
		data.isFall = movement->GetIsFall();
		data.maxSpeed = movement->GetMaxSpeed();
		
	}

	return data;
}

void PhysicX::SetCCTData(const unsigned int& id,const CharacterControllerGetSetData& controllerData)
{
	if (m_characterControllerContainer.find(id) != m_characterControllerContainer.end())
	{
		auto& controller = m_characterControllerContainer[id];
		physx::PxController* pxController = controller->GetController();

		m_characterControllerContainer[id]->ChangeLayerNumber(controllerData.LayerNumber, m_collisionMatrix);
		controller->SetPosition(controllerData.position);
	}
	else {
		for (auto& [controller, movement] : m_updateCCTList) {
			if (controller.id == id)
			{
				controller.position = controllerData.position;
			}
		}
		for (auto& [controller, movement] : m_waittingCCTList)
		{
			if (controller.id == id)
			{
				controller.position = controllerData.position;
			}
		}
	}

}

void PhysicX::SetMovementData(const unsigned int& id,const CharacterMovementGetSetData& movementData)
{
	if (m_characterControllerContainer.find(id) != m_characterControllerContainer.end()) {
		auto& controller = m_characterControllerContainer[id];
		CharacterMovement* movement = controller->GetCharacterMovement();

		controller->SetMoveRestrct(movementData.restrictDirection);
		movement->SetIsFall(movementData.isFall);
		movement->SetVelocity(movementData.velocity);
		movement->SetMaxSpeed(movementData.maxSpeed);
		movement->SetAcceleration(movementData.acceleration);
	}
}

//===================================================
//케릭터 관절 정보 -> 레그돌

void PhysicX::CreateCharacterInfo(const ArticulationInfo& info)
{
	if (m_ragdollContainer.find(info.id) != m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::CreateCharacterInfo() : id is already exist id :" + std::to_string(info.id));
		return;
	}

	RagdollPhysics* ragdoll = new RagdollPhysics();
	CollisionData* collisionData = new CollisionData();

	collisionData->thisId = info.id;
	collisionData->thisLayerNumber = info.layerNumber;

	m_collisionDataContainer.insert(std::make_pair(info.id, collisionData));

	ragdoll->Initialize(info, m_physics, collisionData);
	m_ragdollContainer.insert(std::make_pair(info.id, ragdoll));

}

void PhysicX::RemoveCharacterInfo(const unsigned int& id)
{
	if (m_ragdollContainer.find(id) == m_ragdollContainer.end())
	{
		Debug->LogWarning("remove ragdoll data fail container have not data id:"+std::to_string(id));
		return;
	}

	auto articulationIter = m_ragdollContainer.find(id);
	auto pxArticulation = articulationIter->second->GetPxArticulation();

	if (articulationIter->second->GetIsRagdoll() == true)
	{
		m_scene->removeArticulation(*pxArticulation);
		if (pxArticulation)
		{
			pxArticulation->release();
			pxArticulation = nullptr;
		}
	}

	m_ragdollContainer.erase(articulationIter);
}

void PhysicX::RemoveAllCharacterInfo()
{
	for (auto& [id, ragdoll] : m_ragdollContainer)
	{
		auto pxArticulation = ragdoll->GetPxArticulation();
		if (ragdoll->GetIsRagdoll() == true)
		{
			m_scene->removeArticulation(*pxArticulation);
			if (pxArticulation) {
				pxArticulation->release();
				pxArticulation = nullptr;
			}

			Debug->Log("PhysicX::RemoveAllCharacterInfo() : remove articulation id :" + std::to_string(id));
		}
	}
	m_ragdollContainer.clear();
}

void PhysicX::AddArticulationLink(unsigned int id, LinkInfo& info, const Mathf::Vector3& extent)
{
	if (m_ragdollContainer.find(id) == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::AddArticulationLink() : id is not exist id :" + std::to_string(id));
		return;
	}

	RagdollPhysics* ragdoll = m_ragdollContainer[id];
	ragdoll->AddArticulationLink(info,m_collisionMatrix, extent);
}

void PhysicX::AddArticulationLink(unsigned int id, LinkInfo& info, const float& radius)
{
	if (m_ragdollContainer.find(id) == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::AddArticulationLink() : id is not exist id :" + std::to_string(id));
		return;
	}
	RagdollPhysics* ragdoll = m_ragdollContainer[id];
	ragdoll->AddArticulationLink(info, m_collisionMatrix, radius);
}

void PhysicX::AddArticulationLink(unsigned int id, LinkInfo& info, const float& halfHeight, const float& radius)
{
	if (m_ragdollContainer.find(id) == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::AddArticulationLink() : id is not exist id :" + std::to_string(id));
		return;
	}
	RagdollPhysics* ragdoll = m_ragdollContainer[id];
	ragdoll->AddArticulationLink(info, m_collisionMatrix, halfHeight, radius);
}

void PhysicX::AddArticulationLink(unsigned int id, LinkInfo& info)
{
	if (m_ragdollContainer.find(id) == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::AddArticulationLink() : id is not exist id :" + std::to_string(id));
		return;
	}
	RagdollPhysics* ragdoll = m_ragdollContainer[id];
	ragdoll->AddArticulationLink(info, m_collisionMatrix);
}

ArticulationGetData PhysicX::GetArticulationData(const unsigned int& id)
{
	ArticulationGetData data;
	auto articulationIter = m_ragdollContainer.find(id);
	if (articulationIter == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::GetArticulationData() : id is not exist id :" + std::to_string(id));
		return data;
	}

	auto ragdoll = articulationIter->second;

	physx::PxTransform pxTransform = ragdoll->GetPxArticulation()->getRootGlobalPose();
	Mathf::Matrix dxMatrix;
	CopyMatrixPxToDx(pxTransform, dxMatrix);

	data.WorldTransform = dxMatrix;
	data.bIsRagdollSimulation = ragdoll->GetIsRagdoll();

	for (auto& [name, link] : ragdoll->GetLinkContainer()) {
		ArticulationLinkGetData linkData;
		linkData.jointLocalTransform = link->GetRagdollJoint()->GetSimulLocalTransform();
		linkData.name = link->GetName();

		data.linkData.push_back(linkData);
	}

	return data;
}

void PhysicX::SetArticulationData(const unsigned int& id, const ArticulationSetData& articulationData)
{
	auto articulationIter = m_ragdollContainer.find(id);
	if (articulationIter == m_ragdollContainer.end())
	{
		Debug->LogError("PhysicX::SetArticulationData() : id is not exist id :" + std::to_string(id));
		return;
	}

	auto ragdoll = articulationIter->second;
	
	ragdoll->ChangeLayerNumber(articulationData.LayerNumber, m_collisionMatrix);

	if (articulationData.bIsRagdollSimulation != ragdoll->GetIsRagdoll()) {
		ragdoll->SetIsRagdoll(articulationData.bIsRagdollSimulation);
		ragdoll->SetWorldTransform(articulationData.WorldTransform);

		//레그돌 시뮬레이션을 해야하는 경우
		if (articulationData.bIsRagdollSimulation)
		{
			for (const auto& linkData : articulationData.linkData)
			{
				ragdoll->SetLinkTransformUpdate(linkData.name, linkData.boneWorldTransform);
			}
			auto pxArticulation = ragdoll->GetPxArticulation();
			bool isCheck = m_scene->addArticulation(*pxArticulation);

			if (isCheck == false)
			{
				Debug->LogError("PhysicX::SetArticulationData() : add articulation failed id :" + std::to_string(id));
			}
		}
	}

}

unsigned int PhysicX::GetArticulationCount()
{
	return m_scene->getNbArticulations();
}

void PhysicX::ConnectPVD()
{
    if (pvd != nullptr) {
        if (pvd->isConnected())
            pvd->disconnect();
    }
    pvd = PxCreatePvd(*m_foundation);
    PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
    auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
    std::cout << "pvd connected : " << isconnected << std::endl;
}

void PhysicX::ShowNotRelease()
{
    std::cout << "physx count " << std::endl;
   std::cout << m_physics->getNbBVHs()<< std::endl;
   std::cout << m_physics->getNbBVHs()<< std::endl;
   std::cout << m_physics->getNbConvexMeshes()<< std::endl;
   std::cout << m_physics->getNbDeformableSurfaceMaterials()<< std::endl;
   std::cout << m_physics->getNbDeformableVolumeMaterials()<< std::endl;
   std::cout << m_physics->getNbFEMSoftBodyMaterials()<< std::endl;
   std::cout << m_physics->getNbHeightFields()<< std::endl;
   std::cout << m_physics->getNbMaterials()<< std::endl; // 1
   std::cout << m_physics->getNbPBDMaterials()<< std::endl;
   std::cout << m_physics->getNbScenes()<< std::endl;  // 1
   std::cout << m_physics->getNbShapes()<< std::endl;  // 3
   std::cout << m_physics->getNbTetrahedronMeshes()<< std::endl;
   std::cout << m_physics->getNbTriangleMeshes() << std::endl;
}
