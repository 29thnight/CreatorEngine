#include "Physx.h"
#include "PhysicsCommon.h"
#include "PhysicsHelper.h"
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
#include <cuda_runtime_api.h>

PxFilterFlags CustomFilterShader(
	PxFilterObjectAttributes at0,
	PxFilterData fd0,
	PxFilterObjectAttributes at1,
	PxFilterData fd1,
	PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
	
	if (physx::PxFilterObjectIsTrigger(at0) || physx::PxFilterObjectIsTrigger(at1))
	{
		if (((((1 << fd0.word0) & fd1.word1)) > 0) && (((1 << fd1.word0) & fd0.word1) > 0)) {
			pairFlags = physx::PxPairFlag::eTRIGGER_DEFAULT
				| physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
				| physx::PxPairFlag::eNOTIFY_TOUCH_LOST;
			return physx::PxFilterFlag::eDEFAULT;
		}
	}

	if (((((1 << fd0.word0) & fd1.word1)) > 0) && (((1 << fd1.word0) & fd0.word1) > 0)) {
		pairFlags = physx::PxPairFlag::eCONTACT_DEFAULT
			| physx::PxPairFlag::eDETECT_CCD_CONTACT
			| physx::PxPairFlag::eNOTIFY_TOUCH_CCD
			| physx::PxPairFlag::eNOTIFY_TOUCH_FOUND
			| physx::PxPairFlag::eNOTIFY_TOUCH_LOST
			| physx::PxPairFlag::eNOTIFY_CONTACT_POINTS
			| physx::PxPairFlag::eCONTACT_EVENT_POSE
			| physx::PxPairFlag::eNOTIFY_TOUCH_PERSISTS;
		return physx::PxFilterFlag::eDEFAULT;
	}
	else {
		pairFlags &= ~physx::PxPairFlag::eCONTACT_DEFAULT; 
		return physx::PxFilterFlag::eSUPPRESS;
	}
}

class RaycastQueryFilter : public physx::PxQueryFilterCallback
{
public:
	virtual ~RaycastQueryFilter(){}
	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& filterData,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor,
		physx::PxHitFlags& queryFlags) override {

		auto data = shape->getSimulationFilterData();

		if (filterData.word1 & (1 << data.word0)) {
			return physx::PxQueryHitType::eTOUCH;
		}

		return physx::PxQueryHitType::eNONE;
	}
	
	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData& filterData,
		const physx::PxQueryHit& hit,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor) override {

		auto data = shape->getSimulationFilterData();

		if (filterData.word1 & (1 << data.word0)) {
			return physx::PxQueryHitType::eTOUCH;
		}
		return physx::PxQueryHitType::eNONE;
	}
};

bool PhysicX::Initialize()
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
	if (m_physics == nullptr)
	{
		return false;
	}

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


	//충돌 처리에 대한 콜백 설정
	m_eventCallback = new PhysicsEventCallback();


	// Scene 셍성
	physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);

	
	sceneDesc.cpuDispatcher = gDispatcher;
	//sceneDesc.filterShader = PxDefaultSimulationFilterShader;
	sceneDesc.filterShader = CustomFilterShader;

	sceneDesc.simulationEventCallback = m_eventCallback;

	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS; // 활성 액터만 업데이트
	//sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	// GPU 가속 사용	시 GPU에서 물리 연산을 수행하도록 설정
	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS; // GPU 
	sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU; // GPU 
	sceneDesc.cudaContextManager = m_cudaContextManager;

	m_scene = m_physics->createScene(sceneDesc); //

	m_characterControllerManager = PxCreateControllerManager(*m_scene);


	//======================================================================
	//debug용 plane 생성 --> triangle mesh로 대체 예정
	
	/*physx::PxRigidStatic* plane = m_physics->createRigidStatic(PxTransform(PxQuat(PxPi / 2, PxVec3(0, 0, 1))));
	physx::PxShape* planeShape = m_physics->createShape(physx::PxPlaneGeometry(), *m_defaultMaterial);
	planeShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
	planeShape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
	physx::PxFilterData filterData;
	filterData.word0 = 0;
	filterData.word1 = 0xFFFFFFFF;
	planeShape->setSimulationFilterData(filterData);
	plane->attachShape(*planeShape);
	plane->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, true);
	plane->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
	m_scene->addActor(*plane);
	planeShape->release();*/

	//======================================================================


	return true;
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
	// PxScene 업데이트
	RemoveActors();
	//천 및 의상 시뮬레이션 업데이트 -> 차후 생각
	{
		//콜라이더 업데이트
		//업데이트 할 액터들을 모두 씬에 추가
		for (RigidBody* body : m_updateActors) {
			DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
			if (dynamicBody) {
				m_scene->addActor(*dynamicBody->GetRigidDynamic());
			}
			StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
			if (staticBody) {
				m_scene->addActor(*staticBody->GetRigidStatic());
			}
		}
		m_updateActors.clear();
	}
	{
		//케릭터 업데이트
		//케릭터 컨트롤러 업데이트
		for (const auto& controller : m_characterControllerContainer) {
			controller.second->Update(fixedDeltaTime);
		}
		//생성 예정인 케릭터 컨트롤러를 씬에 추가
		for (auto& [contrllerInfo, movementInfo] : m_updateCCTList)
		{
			CollisionData* collisionData = new CollisionData();

			physx::PxCapsuleControllerDesc desc;

			CharacterController* controller = new CharacterController();
			controller->Initialize(contrllerInfo, movementInfo, m_characterControllerManager, m_defaultMaterial, collisionData, m_collisionMatrix);

			desc.height = contrllerInfo.height;
			desc.radius = contrllerInfo.radius;
			desc.contactOffset = contrllerInfo.contactOffset;
			desc.stepOffset = contrllerInfo.stepOffset;
			desc.slopeLimit = contrllerInfo.slopeLimit;
			desc.position.x = contrllerInfo.position.x;
			desc.position.y = contrllerInfo.position.y;
			desc.position.z = contrllerInfo.position.z;
			desc.maxJumpHeight = 100.0f;
			desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
			desc.material = m_defaultMaterial;

			physx::PxController* pxController = m_characterControllerManager->createController(desc);

			physx::PxRigidDynamic* body = pxController->getActor();
			physx::PxShape* shape;
			int shapeSize = body->getNbShapes();
			body->getShapes(&shape, shapeSize);
			body->setSolverIterationCounts(8, 4);
			shape->setContactOffset(0.02f);
			shape->setRestOffset(0.01f);

			physx::PxFilterData filterData;
			filterData.word0 = contrllerInfo.layerNumber;
			filterData.word1 = m_collisionMatrix[contrllerInfo.layerNumber];
			shape->setSimulationFilterData(filterData);

			collisionData->thisId = contrllerInfo.id;
			collisionData->thisLayerNumber = contrllerInfo.layerNumber;
			shape->userData = collisionData;
			body->userData = collisionData;

			controller->SetController(pxController);

			CreateCollisionData(contrllerInfo.id, collisionData);
			m_characterControllerContainer.insert({ controller->GetID(), controller });
		}
		m_updateCCTList.clear();

		//대기중인 케릭터 컨트롤러를 생성예정 리스트에 추가
		for (auto& controllerInfo : m_waittingCCTList) {
			m_updateCCTList.push_back(controllerInfo);
		}
		m_waittingCCTList.clear();
	}
	{
		//콜리전 업데이트
		//삭제 예정인 콜리전 데이터 삭제
		for (unsigned int& removeID : m_removeCollisionIds) {
			auto iter = m_collisionDataContainer.find(removeID);
			if (iter != m_collisionDataContainer.end()) {
				m_collisionDataContainer.erase(iter);
			}
		}
		m_removeCollisionIds.clear();

		for (auto& collisionData : m_collisionDataContainer) {
			if (collisionData.second->isDead == true)
			{
				m_removeCollisionIds.push_back(collisionData.first);
			}
		}
	}
	{
	//ragdoll 업데이트
		for (auto& [id,ragdoll] : m_ragdollContainer) {
		
			ragdoll->Update(fixedDeltaTime);
		}
	}
	//Scene 시뮬레이션
	if (!m_scene->simulate(fixedDeltaTime))
	{
		Debug->LogCritical("physic m_scene simulate failed");
		return;
	}
	//시뮬레이션 결과 받기
	if (!m_scene->fetchResults(true))
	{
		Debug->LogCritical("physic m_scene fetchResults failed");
		return;
	}
	//콜백 이벤트 처리
	m_eventCallback->StartTrigger();
	//에러 처리
	//if (cudaGetLastError() != cudaError::cudaSuccess)
	//{
	//	Debug->LogError("cudaGetLastError : "+ std::string(cudaGetErrorString(cudaGetLastError())));
	//}
}

void PhysicX::FinalUpdate()
{
	//왜 있는지 모르겠다. 일단 남겨놔 보자	
}

void PhysicX::SetCallBackCollisionFunction(std::function<void(CollisionData, ECollisionEventType)> func)
{
	m_eventCallback->SetCallbackFunction(func);
}

void PhysicX::SetPhysicsInfo()
{
		
	// m_scene->setGravity(PxVec3(0.0f, -9.81f, 0.0f));
	//collisionMatrix[0][0] = true;
}

void PhysicX::ChangeScene()
{
	// 씬 체인지 시에 씬에 있는 모든 물리엔진 객체 삭제
	RemoveAllRigidBody(m_scene,m_removeActorList);
	RemoveAllCCT();
	RemoveAllCharacterInfo();
	//옷 시뮬레이션 삭제도 추가시 삭제
}

RayCastOutput PhysicX::RayCast(const RayCastInput & in, bool isStatic)
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

	RaycastQueryFilter queryFilter;
	bool isAnyHit;
		
	isAnyHit = m_scene->raycast(pxOrgin, pxDirection, in.distance, hitBufferStruct, physx::PxHitFlag::eDEFAULT, filterData,&queryFilter);


	RayCastOutput out;

	//hit가 있는 경우
	if (isAnyHit)
	{
		out.hasBlock = hitBufferStruct.hasBlock;
		if (out.hasBlock)
		{
			const physx::PxRaycastHit& blockHit = hitBufferStruct.block;
			out.id = static_cast<CollisionData*>(hitBufferStruct.block.shape->userData)->thisId;
			CopyVectorPxToDx(hitBufferStruct.block.position, out.blockPosition);
		}

		unsigned int hitSize = hitBufferStruct.nbTouches;
		out.hitSize = hitSize;

		for (unsigned int hitNum = 0; hitNum < hitSize; hitNum++)
		{
			const physx::PxRaycastHit& hit = hitBufferStruct.touches[hitNum];
			physx::PxShape* shape = hit.shape;
			
			DirectX::SimpleMath::Vector3 position;
			CopyVectorPxToDx(hit.position, position);
			unsigned int id = static_cast<CollisionData*>(shape->userData)->thisId;
			unsigned int layerNumber = static_cast<CollisionData*>(shape->userData)->thisLayerNumber;

			out.contectPoints.push_back(position);
			out.hitLayerNumber.push_back(layerNumber);
			out.hitId.push_back(id);
		}
	}
	
	return out;
}
//==========================================================================================
//rigid body

void PhysicX::CreateStaticBody(const BoxColliderInfo & info, const EColliderType & colliderType)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetExtent(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z);
	}
	
}

void PhysicX::CreateStaticBody(const SphereColliderInfo & info, const EColliderType & colliderType)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetRadius(info.radius);
	}
}

void PhysicX::CreateStaticBody(const CapsuleColliderInfo & info, const EColliderType & colliderType)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);

	shape->release();

	if (staticBody != nullptr)
	{
		staticBody->SetRadius(info.radius);
		staticBody->SetHalfHeight(info.height);
	}
}

void PhysicX::CreateStaticBody(const ConvexMeshColliderInfo & info, const EColliderType & colliderType)
{
	ConvexMeshResource* convexMesh = new ConvexMeshResource(m_physics, info.vertices, info.vertexSize, info.convexPolygonLimit);
	physx::PxConvexMesh* pxConvexMesh = convexMesh->GetConvexMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);
	shape->release();
}

void PhysicX::CreateStaticBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices,info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);

	shape->release();	
}

void PhysicX::CreateStaticBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols,info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField,physx::PxMeshGeometryFlag::eDOUBLE_SIDED), *material);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);
	staticBody->SetOffsetRotation(DirectX::SimpleMath::Matrix::CreateRotationZ(180.0f / 180.0f * 3.14f));
	staticBody->SetOffsetTranslation(DirectX::SimpleMath::Matrix::CreateTranslation(DirectX::SimpleMath::Vector3(info.rowScale * info.numRows * 0.5f, 0.0f, -info.colScale * info.numCols * 0.5f)));

	shape->release();

}

void PhysicX::CreateDynamicBody(const BoxColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material);

	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);

	shape->release();

	if (dynamicBody != nullptr)
	{
		dynamicBody->SetExtent(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z);
	}
}

void PhysicX::CreateDynamicBody(const SphereColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
	if (dynamicBody != nullptr)
	{
		dynamicBody->SetRadius(info.radius);
	}
}

void PhysicX::CreateDynamicBody(const CapsuleColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
	if (dynamicBody != nullptr)
	{
		dynamicBody->SetRadius(info.radius);
		dynamicBody->SetHalfHeight(info.height);
	}
}

void PhysicX::CreateDynamicBody(const ConvexMeshColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	/*static int number = 0;
	number++;*/

	ConvexMeshResource* convexMesh = new ConvexMeshResource(m_physics, info.vertices, info.vertexSize, info.convexPolygonLimit);
	physx::PxConvexMesh* pxConvexMesh = convexMesh->GetConvexMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material);

	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType, bool isKinematic)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices, info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material);

	shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE,true);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols, info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField), *material);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

StaticRigidBody* PhysicX::SettingStaticBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, int* collisionMatrix)
{
	//filterData
	physx::PxFilterData filterData;
	filterData.word0 = colInfo.layerNumber;
	 //filterData.word1 = collisionMatrix[colInfo.layerNumber];
	filterData.word1 = 0xFFFFFFFF;
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
	//filterData.word1 = collisionMatrix[colInfo.layerNumber];
	filterData.word1 = 0xFFFFFFFF;
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


RigidBodyGetSetData PhysicX::GetRigidBodyData(unsigned int id)
{
	RigidBodyGetSetData rigidBodyData;

	auto body = m_rigidBodyContainer.find(id)->second;
	
	//dynamicBody 의 경우
	DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
	if (dynamicBody)
	{
		physx::PxRigidDynamic* pxBody = dynamicBody->GetRigidDynamic();
		DirectX::SimpleMath::Matrix dxMatrix;
		CopyMatrixPxToDx(pxBody->getGlobalPose(), dxMatrix);
		rigidBodyData.transform = DirectX::SimpleMath::Matrix::CreateScale(dynamicBody->GetScale()) * dxMatrix * dynamicBody->GetOffsetTranslation();
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
		DirectX::SimpleMath::Matrix dxMatrix;
		CopyMatrixPxToDx(pxBody->getGlobalPose(), dxMatrix);
		rigidBodyData.transform = DirectX::SimpleMath::Matrix::CreateScale(staticBody->GetScale()) * staticBody->GetOffsetRotation() *dxMatrix * staticBody->GetOffsetTranslation();
	}

	return rigidBodyData;
}

void PhysicX::SetRigidBodyData(const unsigned int& id, const RigidBodyGetSetData& rigidBodyData)
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
		DirectX::SimpleMath::Matrix dxMatrix = rigidBodyData.transform;
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

		DirectX::SimpleMath::Vector3 position;
		DirectX::SimpleMath::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		DirectX::SimpleMath::Quaternion rotation;
		dxMatrix.Decompose(scale, rotation, position);
		dxMatrix = DirectX::SimpleMath::Matrix::CreateScale(1.0f) * DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) * DirectX::SimpleMath::Matrix::CreateTranslation(position);

		CopyMatrixDxToPx(dxMatrix, pxTransform);
		pxBody->setGlobalPose(pxTransform);
		dynamicBody->ChangeLayerNumber(rigidBodyData.LayerNumber, m_collisionMatrix);

		if (scale.x>0.0f&&scale.y>0.0f&&scale.z>0.0f)
		{
			dynamicBody->SetConvertScale(scale, m_physics, m_collisionMatrix);
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
		DirectX::SimpleMath::Matrix dxMatrix = rigidBodyData.transform;
		physx::PxTransform pxPrevTransform = pxBody->getGlobalPose();
		physx::PxTransform pxCurrTransform;
		
		DirectX::SimpleMath::Vector3 position;
		DirectX::SimpleMath::Vector3 scale = { 1.0f, 1.0f, 1.0f };
		DirectX::SimpleMath::Quaternion rotation;

		dxMatrix.Decompose(scale, rotation, position);
		dxMatrix = DirectX::SimpleMath::Matrix::CreateScale(1.0f) * 
			DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) * staticBody->GetOffsetRotation().Invert() *
			DirectX::SimpleMath::Matrix::CreateTranslation(position) * staticBody->GetOffsetTranslation().Invert();

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
	if (m_characterControllerContainer.find(id) != m_characterControllerContainer.end())
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
	if (m_characterControllerContainer.find(id) != m_characterControllerContainer.end())
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

void PhysicX::AddArticulationLink(unsigned int id, LinkInfo& info, const DirectX::SimpleMath::Vector3& extent)
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
	DirectX::SimpleMath::Matrix dxMatrix;
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

void PhysicX::CollisionDataUpdate()
{
	//삭제예정 데이터 삭제
	for (auto& removeId : m_removeCollisionIds) {
		auto iter = m_collisionDataContainer.find(removeId);
		if (iter != m_collisionDataContainer.end())
		{
			m_collisionDataContainer.erase(iter);
		}
	}
	m_removeCollisionIds.clear();

	//충돌 삭제 예정 데이터 등록
	for (auto& collisionIter : m_collisionDataContainer) {
		if (collisionIter.second->isDead == true) {
			m_removeCollisionIds.push_back(collisionIter.first);
		}
	}
}

CollisionData* PhysicX::FindCollisionData(const unsigned int& id)
{
	auto iter = m_collisionDataContainer.find(id);
	if (iter != m_collisionDataContainer.end())
	{
		return iter->second;
	}
	else
	{
		Debug->LogError("PhysicX::FindCollisionData() : id is not exist id :" + std::to_string(id));
		return nullptr;
	}
}

void PhysicX::CreateCollisionData(const unsigned int& id, CollisionData* data)
{
	m_collisionDataContainer.insert(std::make_pair(id, data));
}

void PhysicX::RemoveCollisionData(const unsigned int& id)
{
	//충돌 데이터가 등록되어 있는지 검사
	for (unsigned int& removeId : m_removeCollisionIds) {
		if (removeId == id) {
			return;
		}
	}

	m_removeCollisionIds.push_back(id);
}

void PhysicX::extractDebugConvexMesh(physx::PxRigidActor* body, physx::PxShape* shape, std::vector<std::vector<DirectX::SimpleMath::Vector3>>& debuPolygon)
{
	const physx::PxConvexMeshGeometry& convexMeshGeom = static_cast<const physx::PxConvexMeshGeometry&>(shape->getGeometry());

	DirectX::SimpleMath::Matrix dxMatrix;
	DirectX::SimpleMath::Matrix matrix;

	std::vector<std::vector<DirectX::SimpleMath::Vector3>> polygon;

	//convexMesh의 정점과 인덱스 정보 가져오기
	const physx::PxVec3* convexMeshVertices = convexMeshGeom.convexMesh->getVertices();
	const physx::PxU32 vertexCount = convexMeshGeom.convexMesh->getNbVertices();

	const physx::PxU8* convexMeshIndices = convexMeshGeom.convexMesh->getIndexBuffer();
	const physx::PxU32 polygonCount = convexMeshGeom.convexMesh->getNbPolygons();

	//내보내기 위한 polygon 정보 생성
	polygon.reserve(polygonCount);

	for (PxU32 i = 0; i < polygonCount-1; i++)
	{
		physx::PxHullPolygon pxPolygon;
		convexMeshGeom.convexMesh->getPolygonData(i, pxPolygon);
		int totalVertexCount = pxPolygon.mNbVerts;
		int startIndex = pxPolygon.mIndexBase;

		std::vector<DirectX::SimpleMath::Vector3> vertices;
		vertices.reserve(totalVertexCount);

		for (int j = 0; j < totalVertexCount; j++)
		{
			DirectX::SimpleMath::Vector3 vertex;
			vertex.x = convexMeshVertices[convexMeshIndices[startIndex + j]].x;
			vertex.y = convexMeshVertices[convexMeshIndices[startIndex + j]].y;
			vertex.z = convexMeshVertices[convexMeshIndices[startIndex + j]].z;

			vertex = DirectX::SimpleMath::Vector3::Transform(vertex, dxMatrix);

			vertices.push_back(vertex);
		}

		debuPolygon.push_back(vertices);
	}

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
