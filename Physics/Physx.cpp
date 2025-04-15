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

static PhysicsEventCallback eventCallback;

// block(default), player, enemy, interact
bool collisionMatrix[4][4] = {
	{true, true, true, true},
	{true, true, true, true},
	{true, true, false, false},
	{true, true, false, false}
};

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

	if (!collisionMatrix[group0][group1]) {
		return PxFilterFlag::eSUPPRESS; 
	}


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
void PhysicX::UnInitialize() {
	if (m_foundation) m_foundation->release();
}

void PhysicX::PreUpdate() {}



void PhysicX::ReadPhysicsData()
{
	PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);

	// 
	PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
	std::vector<PxActor*> actors(curActorCount);
	m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);
	auto& writeBuffer = bufferPool.GetWriteBuffer();
	writeBuffer.resize(curActorCount);

	for (int i = 0; i < curActorCount; i++) {
		auto dynamicActor = static_cast<PxRigidDynamic*>(actors[i]);
		IRigidbody* rb = static_cast<IRigidbody*>(actors[i]->userData);

		RigidbodyInfo* info = rb->GetInfo();

		if (info->isCharacterController) continue;

		//if (info->changeflag == false) continue;  //
		//info->changeflag = false;                 // 

		// 
		dynamicActor->setMass(info->mass);
		dynamicActor->setLinearDamping(info->drag);
		dynamicActor->setAngularDamping(info->angularDrag);
		dynamicActor->setCMassLocalPose(PxTransform(info->centerOfMass[0], info->centerOfMass[1], info->centerOfMass[2]));
		dynamicActor->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, !info->useGravity);
		dynamicActor->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, info->isKinematic);

		// 
		auto [posX, posY, posZ] = rb->GetWorldPosition();
		auto [rotX, rotY, rotZ, rotW] = rb->GetWorldRotation();

		// 
		info->prePosition[0] = posX;
		info->prePosition[1] = posY;
		info->prePosition[2] = posZ;

		info->preRotation[0] = rotX;
		info->preRotation[1] = rotY;
		info->preRotation[2] = rotZ;
		info->preRotation[3] = rotW;

		PxTransform p({ posX, posY, posZ }, { rotX, rotY, rotZ, rotW });
		//if (info->isKinematic)
		//    dynamicActor->setKinematicTarget(p); // 
		//else
		if (!info->isCharacterController)
			dynamicActor->setGlobalPose(p);     // 
		if (!rb) continue;

		RigidbodyInfo* info = writeBuffer[i];
		{
			//
			PxTransform transform = dynamicActor->getGlobalPose();

			info->prePosition[0] = info->Position[0];
			info->prePosition[1] = info->Position[1];
			info->prePosition[2] = info->Position[2];

			info->preRotation[0] = info->Rotation[0];
			info->preRotation[1] = info->Rotation[1];
			info->preRotation[2] = info->Rotation[2];
			info->preRotation[3] = info->Rotation[3];

			info->Position[0] = transform.p.x;
			info->Position[1] = transform.p.y;
			info->Position[2] = transform.p.z;

			info->Rotation[0] = transform.q.x;
			info->Rotation[1] = transform.q.y;
			info->Rotation[2] = transform.q.z;
			info->Rotation[3] = transform.q.w;

			info->linearVelocity[0] = dynamicActor->getLinearVelocity().x;
			info->linearVelocity[1] = dynamicActor->getLinearVelocity().y;
			info->linearVelocity[2] = dynamicActor->getLinearVelocity().z;

			info->angularVelocity[0] = dynamicActor->getAngularVelocity().x;
			info->angularVelocity[1] = dynamicActor->getAngularVelocity().y;
			info->angularVelocity[2] = dynamicActor->getAngularVelocity().z;
		}

		// 
		if (info->useNewPosition) {
			dynamicActor->setGlobalPose(PxTransform({ info->newPosition[0], info->newPosition[1], info->newPosition[2] }));
			info->useNewPosition = false;
		}

		// 
		if (info->useForce) {
			dynamicActor->addForce({ info->force[0], info->force[1], info->force[2] });
			info->useForce = false;
		}

	}

}




void PhysicX::Update(float fixedDeltaTime)
{

	//if (fixedDeltaTime > 0.f) {
	//    m_scene->simulate(fixedDeltaTime);
	//    /////-----------------------占쏙옙-------------------------
	//    m_scene->fetchResults(true);    
	//    m_scene->fetchResultsParticleSystem();
	//    //m_scene->fetchResults();

	//    ReadPhysicsData();  //
	//    bufferPool.SwapBuffers();
	//}
	//==========================

	//
	RemoveActors();
	//todo: 
	//deformer surface 
	//ragdoll

	m_scene->simulate(fixedDeltaTime);
	m_scene->fetchResults(true);

	//todo: cuda 
	m_scene->fetchResultsParticleSystem();

	//
	eventCallback.onSimulationEvent();

	//cuda
	if (cudaGetLastError() != cudaError::cudaSuccess) {
		std::cout << "CUDA error: " << cudaGetErrorString(cudaGetLastError()) << std::endl;
	}

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

	pxOrgin = { in.origin.x, in.origin.y, in.origin.z };
	pxDirection = { in.direction.x, in.direction.y, in.direction.z };

	// RaycastHit 
	const physx::PxU32 maxHits = 20;
	physx::PxRaycastHit hitBuffer[maxHits];
	physx::PxRaycastBuffer hitBufferStruct(hitBuffer, maxHits);

	//占쏙옙占쏙옙 占쏙옙占쏙옙
	physx::PxQueryFilterData filterData;
	filterData.data.word0 = in.layerNumber;
	filterData.data.word1 = collisionMatrix[in.layerNumber][in.layerNumber];

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
	if (!dynamicBody->Initialize(colInfo, shape, m_physics, collisionData))
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

void PhysicX::PostUpdate() {}





physx::PxRigidDynamic* PhysicX::CreateDynamicActor(const physx::PxTransform& transform)
{
	// ??????? ???? actor???? ?????? ???.
	PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
	std::vector<PxActor*> actors(curActorCount);
	m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);
	return m_physics->createRigidDynamic(transform);
}


physx::PxRigidStatic* PhysicX::CreateStaticActor(const physx::PxTransform & transform)
{
	return m_physics->createRigidStatic(transform);
}


// if (info->isKinematic == true)
	//    continue; /
physx::PxShape* PhysicX::CreateBoxShape(Mathf::Vector3 size, physx::PxMaterial * material)
{
	if (material == nullptr) material = m_defaultMaterial;
	return m_physics->createShape(PxBoxGeometry(size.x, size.y, size.z), *material);
}

physx::PxShape* PhysicX::CreateSphereShape(float radius, physx::PxMaterial * material)
{
	if (material == nullptr) material = m_defaultMaterial;
	return m_physics->createShape(PxSphereGeometry(radius), *material);
}

	
physx::PxShape* PhysicX::CreateCapsuleShape(float radius, float halfHeight, physx::PxMaterial * material)
{
	if (material == nullptr) material = m_defaultMaterial;
	return m_physics->createShape(PxCapsuleGeometry(radius, halfHeight), *material);
}



physx::PxShape* PhysicX::CreatePlaneShape(physx::PxMaterial * material)
{
	if (material == nullptr) material = m_defaultMaterial;
	return m_physics->createShape(PxPlaneGeometry(), *material);
}

void PhysicX::AddRigidBody()
{
}


void PhysicX::ClearActors()
{

	PxU32 numMeshes = m_physics->getNbConvexMeshes();
	std::vector<PxConvexMesh*> meshes(numMeshes);
	m_physics->getConvexMeshes(meshes.data(), numMeshes);
	void PhysicX::ClearActors();
	
}

//Physics->ShowNotRelease();

//m_scene->flushSimulation(true);

//// controller 
//PxU32 numControllers = m_controllerManager->getNbControllers();
//for (PxU32 i = 0; i < numControllers; ++i) {
//    PxController* controller = m_controllerManager->getController(i);
//    controller->release();
//}
//m_controllerManager->purgeControllers();

//// actor 
//PxU32 actorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC);
//std::vector<PxActor*> actors(actorCount);
//m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC, actors.data(), actorCount);
//for (PxActor* actor : actors) {
//    m_scene->removeActor(*actor);
//    actor->release();
//}

// 
//UnInitialize();
//if (m_scene) {
//    m_scene->fetchResults(true);  

//for (PxU32 i = 0; i < m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC); i++) {
//    PxActor* actor;
//    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC | PxActorTypeFlag::eRIGID_STATIC, &actor, 1, i);
//    m_scene->removeActor(*actor);
//    actor->release();

////
//if (m_controllerManager) {
//    const PxU32 controllerCount = m_controllerManager->getNbControllers();
//    for (PxI32 i = static_cast<PxI32>(controllerCount) - 1; i >= 0; --i) {
//        PxController* controller = m_controllerManager->getController(i);
//        if (controller) {
//            controller->release();
//            controller = nullptr;  //
//        }
//    }
//    m_controllerManager->release();
//    m_controllerManager = nullptr;
//}

//// 
//if (gDispatcher) {
//    gDispatcher->release();
//    gDispatcher = nullptr;
//}

//// 4
//if (m_defaultMaterial) {
//    m_defaultMaterial->release();
//    m_defaultMaterial = nullptr;
//}

//// 
//if (m_scene) {
//    m_scene->release();
//    m_scene = nullptr;
//}

//// 6? Physics
//if (m_physics) {
//    m_physics->release();
//    m_physics = nullptr;
//}

//// 
//if (pvd) {
//    if (pvd->isConnected()) {
//        pvd->disconnect();  //
//    pvd->release();
//    pvd = nullptr;
//}

//// 8?
//if (m_foundation) {
//    m_foundation->release();
//    m_foundation = nullptr;
//}

    //Initialize();


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

void PhysicX::GetShapes(std::vector<BoxShape>& out)
{
    PxU32 curActorCount = m_scene->getNbActors(PxActorTypeFlag::eRIGID_DYNAMIC);
    std::vector<PxActor*> actors(curActorCount);
    m_scene->getActors(PxActorTypeFlag::eRIGID_DYNAMIC, actors.data(), curActorCount);

    for (PxU32 i = 0; i < curActorCount; i++)
    {
        PxRigidActor* actor = static_cast<PxRigidActor*>(actors[i]);
        PxTransform globalPose = actor->getGlobalPose();

        PxU32 shapeCount = actor->getNbShapes();
        std::vector<PxShape*> shapes(shapeCount);
        actor->getShapes(shapes.data(), shapeCount);

        for (PxU32 j = 0; j < shapeCount; j++)
        {
            PxShape* shape = shapes[j];
            PxTransform localPose = shape->getLocalPose();
            PxTransform worldColliderPose = globalPose * localPose;

            PxBoxGeometry box;
            if (shape->getGeometry().getType() == PxGeometryType::eBOX)
            {
                PxGeometryHolder holder(shape->getGeometry());
                
                auto extent = holder.box().halfExtents;
                BoxShape boxshape;
                boxshape.worldPosition[0] = worldColliderPose.p.x / 10.f;
                boxshape.worldPosition[1] = worldColliderPose.p.y / 10.f;
                boxshape.worldPosition[2] = worldColliderPose.p.z / 10.f;
                boxshape.worldRotation[0] = worldColliderPose.q.x;
                boxshape.worldRotation[1] = worldColliderPose.q.y;
                boxshape.worldRotation[2] = worldColliderPose.q.z;
                boxshape.worldRotation[3] = worldColliderPose.q.w;
                boxshape.halfSize[0] = extent.x / 10.f;
                boxshape.halfSize[1] = extent.y / 10.f;
                boxshape.halfSize[2] = extent.z / 10.f;

                out.push_back(boxshape);
            }
        }
    }
}
