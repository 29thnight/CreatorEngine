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

#include <cuda_runtime.h>
#include <cuda_runtime_api.h>

PxFilterFlags CustomFilterShader(
	PxFilterObjectAttributes at0,
	PxFilterData fd0,
	PxFilterObjectAttributes at1,
	PxFilterData fd1,
	PxPairFlags& pairFlags, const void* constantBlock, PxU32 constantBlockSize)
{
	
	if (PxFilterObjectIsTrigger(at0) || PxFilterObjectIsTrigger(at1)) {
		if ((fd0.word1 & (1 << fd1.word0)) && (fd1.word1 & (1 << fd0.word0))) {
			pairFlags = PxPairFlag::eTRIGGER_DEFAULT
				| PxPairFlag::eNOTIFY_TOUCH_FOUND
				| PxPairFlag::eNOTIFY_TOUCH_LOST;
			return PxFilterFlag::eDEFAULT;
		}
		return PxFilterFlag::eSUPPRESS;
	}
	else {
		if ((fd0.word1 & (1 << fd1.word0)) && (fd1.word1 & (1 << fd0.word0))) {
			pairFlags = PxPairFlag::eCONTACT_DEFAULT
				| PxPairFlag::eNOTIFY_CONTACT_POINTS
				| PxPairFlag::eNOTIFY_TOUCH_FOUND
				| PxPairFlag::eNOTIFY_TOUCH_LOST
				| PxPairFlag::eNOTIFY_TOUCH_PERSISTS;;
			return PxFilterFlag::eDEFAULT;
		}
		return PxFilterFlag::eSUPPRESS;
	}
}

class BlockRaycastQueryFilter : public physx::PxQueryFilterCallback {
public:
	virtual ~BlockRaycastQueryFilter() {}
	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& filterData,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor,
		physx::PxHitFlags& queryFlags) override {

		//auto data = shape->getSimulationFilterData();
		auto data = shape->getQueryFilterData();

		if (filterData.word0 & (1 << data.word0)) {
			return physx::PxQueryHitType::eBLOCK;
		}

		return physx::PxQueryHitType::eNONE;

	}

	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData& filterData,
		const physx::PxQueryHit& hit,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor) override {

		//auto data = shape->getSimulationFilterData();
		auto data = shape->getQueryFilterData();

		if (filterData.word0 & (1 << data.word0)) {
			return physx::PxQueryHitType::eBLOCK;
		}
		return physx::PxQueryHitType::eNONE;
	}
};

class TouchRaycastQueryFilter : public physx::PxQueryFilterCallback
{
public:
	virtual ~TouchRaycastQueryFilter(){}
	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& filterData,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor,
		physx::PxHitFlags& queryFlags) override {

		//auto data = shape->getSimulationFilterData();
		auto data = shape->getQueryFilterData();

		if (filterData.word0 & (1 << data.word0)) {
			return physx::PxQueryHitType::eTOUCH;
		}

		return physx::PxQueryHitType::eNONE;

	}
	
	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData& filterData,
		const physx::PxQueryHit& hit,
		const physx::PxShape* shape,
		const physx::PxRigidActor* actor) override {

		//auto data = shape->getSimulationFilterData();
		auto data = shape->getQueryFilterData();

		if (filterData.word0 & (1 << data.word0)) {
			return physx::PxQueryHitType::eTOUCH;
		}
		return physx::PxQueryHitType::eNONE;
	}
};


// 내부 구현용 쿼리 필터 콜백 클래스
class QueryBlockFilterCallback : public physx::PxQueryFilterCallback
{
public:
	virtual physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& queryFilterData, const physx::PxShape* shape, const physx::PxRigidActor* actor, physx::PxHitFlags& queryFlags) override
	{
		const physx::PxFilterData& shapeFilterData = shape->getQueryFilterData();
		if (queryFilterData.word0 & shapeFilterData.word2) return physx::PxQueryHitType::eBLOCK;
		return physx::PxQueryHitType::eNONE;
	}
	virtual physx::PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override { return physx::PxQueryHitType::eBLOCK; }
};

class QueryTouchFilterCallback : public physx::PxQueryFilterCallback
{
public:
	virtual physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData& queryFilterData, const physx::PxShape* shape, const physx::PxRigidActor* actor, physx::PxHitFlags& queryFlags) override
	{
		const physx::PxFilterData& shapeFilterData = shape->getQueryFilterData();
		if (queryFilterData.word0 & shapeFilterData.word2) return physx::PxQueryHitType::eTOUCH;
		return physx::PxQueryHitType::eNONE;
	}
	virtual physx::PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override { return physx::PxQueryHitType::eTOUCH; }
};

bool PhysicX::Initialize()
{
	m_foundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_allocator, m_errorCallback);     

#if _DEBUG
	// PVD 연결
	// PVD는 _DEBUG가 아니면 nullptr
	pvd = PxCreatePvd(*m_foundation);
	PxPvdTransport* transport = PxDefaultPvdSocketTransportCreate("127.0.0.1", 5425, 10);
	auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::eALL);
#endif

	//m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(1.f, 40.f), recordMemoryAllocations, pvd);  
	m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_foundation, physx::PxTolerancesScale(1.f, 10.f), true, pvd);  // 물리 초기화
	if (m_physics == nullptr)
	{
		return false;
	}

	// physics 코어 개수
	UINT MaxThread = 8;
	UINT core = std::thread::hardware_concurrency();
	// 현재 코어 개수가 최대 코어 개수보다 많으면 최대 코어 개수 사용
	if (core < 4) core = 2;
	else if (core > MaxThread + 4) core = MaxThread;
	else core -= 4;
	// 디스패처 생성
	gDispatcher = PxDefaultCpuDispatcherCreate(core);

	m_defaultMaterial = m_physics->createMaterial(1.f, 1.f, 0.f);

	//gDispatcher = PxDefaultCpuDispatcherCreate(2);  
	// CUDA 초기화
	PxCudaContextManagerDesc cudaDesc;
	/*m_cudaContextManager = PxCreateCudaContextManager(*m_foundation, cudaDesc);
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
	}*/



	m_cudaContextManager = PxCreateCudaContextManager(*m_foundation, cudaDesc);
	if (!m_cudaContextManager || !m_cudaContextManager->contextIsValid())
	{
		if (m_cudaContextManager)
			m_cudaContextManager->release();
		m_cudaContextManager = nullptr;

		// 실패해도 그냥 넘어가기 (로그만 찍고)
		std::cout << "CUDA context manager creation failed, continuing without CUDA." << std::endl;
	}

	// CUDA 컨텍스트가 있으면 사용
	if (m_cudaContextManager) {
		m_cudaContext = m_cudaContextManager->getCudaContext();
		if (m_cudaContext) {
			std::cout << "CUDA context created successfully." << std::endl;
		}
		else {
			std::cout << "Failed to create CUDA context." << std::endl;
			// 실패해도 그냥 넘어가기
		}
	}
	else {
		// CUDA 사용 안 하고 넘어가기
	}

	//충돌 처리를 위한 콜백 등록
	m_eventCallback = new PhysicsEventCallback();
	m_blockCallback = new QueryBlockFilterCallback();
	m_touchCallback = new QueryTouchFilterCallback();

	// Scene 생성
	physx::PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
	sceneDesc.gravity = physx::PxVec3(0.0f, -9.81f, 0.0f);

	
	sceneDesc.cpuDispatcher = gDispatcher;
	//sceneDesc.filterShader = PxDefaultSimulationFilterShader;
	sceneDesc.filterShader = CustomFilterShader;

	sceneDesc.simulationEventCallback = m_eventCallback;

	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS; // 활성 액터를 업데이트
	//sceneDesc.kineKineFilteringMode = PxPairFilteringMode::eKEEP;
	// GPU 시뮬레이션 사용 여부 GPU에서 시뮬레이션이 가능하도록 설정
	sceneDesc.flags |= physx::PxSceneFlag::eENABLE_GPU_DYNAMICS; // GPU 사용
	sceneDesc.broadPhaseType = PxBroadPhaseType::eGPU; // GPU 사용
	sceneDesc.cudaContextManager = m_cudaContextManager;

	m_scene = m_physics->createScene(sceneDesc); // 씬 생성

	m_characterControllerManager = PxCreateControllerManager(*m_scene);

	//======================================================================
	//debug용 plane 생성 --> triangle mesh로 객체 생성
	
	physx::PxRigidStatic* plane = m_physics->createRigidStatic(PxTransform(PxQuat(PxPi / 2, PxVec3(0, 0, 1))));
	auto material = m_defaultMaterial;
	material->setRestitution(0.0f);
	material->setDynamicFriction(1.0f);
	material->setStaticFriction(1.0f);
	physx::PxShape* planeShape = m_physics->createShape(physx::PxPlaneGeometry(), *material);
	planeShape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
	planeShape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
	physx::PxFilterData filterData;
	filterData.word0 = 0;
	filterData.word1 = 0xFFFFFFFF;
	filterData.word2 = 1 << 0;
	planeShape->setSimulationFilterData(filterData);
	planeShape->setQueryFilterData(filterData);
	plane->attachShape(*planeShape);
	plane->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, true);
	plane->setActorFlag(physx::PxActorFlag::eVISUALIZATION, true);
	m_scene->addActor(*plane);
	planeShape->release();

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
	if (m_scene) m_scene->release();
	if (gDispatcher) gDispatcher->release();
	if (m_physics) m_physics->release();
	/*if (m_pvd) {
		m_pvd->release();
	}*/
	if (m_foundation) m_foundation->release();
	delete m_eventCallback;
	delete m_blockCallback;
	delete m_touchCallback;

}


void PhysicX::Update(float fixedDeltaTime)
{
	// PxScene 업데이트
	RemoveActors();
	//첫 틱 마다 시뮬레이션 업데이트 -> 물리 업데이트
	//rigid body 업데이트
	{
		//콜라이더 업데이트
		//업데이트 할 액터들을 씬에 추가
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
		//캐릭터 업데이트
		//캐릭터 컨트롤러 업데이트
		for (const auto& controller : m_characterControllerContainer) {
			controller.second->Update(fixedDeltaTime);
			//컨트롤러의 물리 상태가 변하면
			//if(todo : condition){
			//controller.second->GetController()->release();
			// }

		}
		//새로 생성된 캐릭터 컨트롤러를 업데이트 리스트에 추가
		for (auto& [contrllerInfo, movementInfo] : m_updateCCTList)
		{
			CollisionData* collisionData = new CollisionData();

			physx::PxCapsuleControllerDesc desc;

			CharacterController* controller = new CharacterController();
			controller->Initialize(contrllerInfo, movementInfo, m_characterControllerManager, m_defaultMaterial, collisionData, m_collisionMatrix, m_collisionCallback);

			desc.height = contrllerInfo.height;
			desc.radius = contrllerInfo.radius;
			desc.contactOffset = contrllerInfo.contactOffset;
			desc.stepOffset = contrllerInfo.stepOffset;
			desc.slopeLimit = contrllerInfo.slopeLimit;
			desc.position.x = contrllerInfo.position.x;
			desc.position.y = contrllerInfo.position.y;
			desc.position.z = contrllerInfo.position.z; 
			desc.scaleCoeff = 1.0f;
			desc.maxJumpHeight = 100.0f;
			desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
			desc.material = m_defaultMaterial;
			desc.reportCallback = controller->GetHitReportCallback();
			physx::PxController* pxController = m_characterControllerManager->createController(desc);
			
			physx::PxRigidDynamic* body = pxController->getActor();
			physx::PxShape* shape;
			int shapeSize = body->getNbShapes();
			body->getShapes(&shape, shapeSize);
			body->setSolverIterationCounts(8, 4);
			shape->setContactOffset(0.3f);
			shape->setRestOffset(0.02f);
			physx::PxFilterData filterData;
			filterData.word0 = contrllerInfo.layerNumber;
			filterData.word1 = m_collisionMatrix[contrllerInfo.layerNumber];
			filterData.word2 = 1 << contrllerInfo.layerNumber;
			//
			shape->setSimulationFilterData(filterData);
			shape->setQueryFilterData(filterData);
			//shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false);
			//shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true); //&&&&&sehwan

			collisionData->thisId = contrllerInfo.id;
			collisionData->thisLayerNumber = contrllerInfo.layerNumber;
			shape->userData = collisionData;
			body->userData = collisionData;

			controller->SetController(pxController);

			CreateCollisionData(contrllerInfo.id, collisionData);
			m_characterControllerContainer.insert({ controller->GetID(), controller });
		}
		m_updateCCTList.clear();

		//대기중인 캐릭터 컨트롤러를 업데이트 리스트에 추가
		for (auto& controllerInfo : m_waittingCCTList) {
			m_updateCCTList.push_back(controllerInfo);
		}
		m_waittingCCTList.clear();
	}
	{
		//충돌 업데이트
		//새로 생성된 충돌 데이터를 제거
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
	//시뮬레이션 결과 가져오기
	if (!m_scene->fetchResults(true))
	{
		Debug->LogCritical("physic m_scene fetchResults failed");
		return;
	}
	//콜백 이벤트 처리
	m_eventCallback->StartTrigger();
	//오류 처리
	//if (cudaGetLastError() != cudaError::cudaSuccess)
	//{
	//	Debug->LogError("cudaGetLastError : "+ std::string(cudaGetErrorString(cudaGetLastError())));
	//}
}

void PhysicX::FinalUpdate()
{
	//딱 한번만 불린다. 아마도 씬이 끝날때
}

void PhysicX::SetCallBackCollisionFunction(std::function<void(const CollisionData&, ECollisionEventType)> func)
{
	m_eventCallback->SetCallbackFunction(func);
	m_collisionCallback = func;
}

void PhysicX::SetPhysicsInfo()
{
		
	// m_scene->setGravity(PxVec3(0.0f, -9.81f, 0.0f));
	//collisionMatrix[0][0] = true;기본 모든 레이어가 충돌하는 상태로 설정
}

void PhysicX::ChangeScene()
{
	// 모든 객체를 씬에서 제거하고 물리 엔진 객체 제거
	RemoveAllRigidBody(m_scene,m_removeActorList);
	RemoveAllCCT();
	RemoveAllCharacterInfo();
	//새로운 시뮬레이션에 추가될 객체
}

void PhysicX::DestroyActor(unsigned int id)
{
	//컨테이너에 있는지 확인
	auto iter = m_rigidBodyContainer.find(id);
	if (iter == m_rigidBodyContainer.end())
	{
		//std::cout << "PhysicX::DestroyActor : Actor not found with id" << id << std::endl;
		return;
	}

	RigidBody* body = iter->second;

	if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(body))
	{
		//다이나믹 바디인 경우
		m_removeActorList.push_back(dynamicBody->GetRigidDynamic());
	}
	else if (auto* staticBody = dynamic_cast<StaticRigidBody*>(body))
	{
		//스테틱 바디인 경우
		m_removeActorList.push_back(staticBody->GetRigidStatic());
	}
	else
	{
		Debug->LogError("PhysicX::DestroyActor : Actor is not a valid RigidBody type");
		return;
	}

	delete body; // 메모리 해제
	m_rigidBodyContainer.erase(iter); // 컨테이너에서 제거

	// 충돌 데이터 제거
	RemoveCollisionData(id);
}

RayCastOutput PhysicX::RayCast(const RayCastInput& in, bool isStatic)
{
	physx::PxVec3 pxOrgin;
	physx::PxVec3 pxDirection;
	ConvertVectorDxToPx(in.origin, pxOrgin);
	ConvertVectorDxToPx(in.direction, pxDirection);

	// RaycastHit 
	const physx::PxU32 maxHits = 20;
	physx::PxRaycastHit hitBuffer[maxHits];
	physx::PxRaycastBuffer hitBufferStruct(hitBuffer, maxHits);

	//충돌 필터 데이터
	physx::PxQueryFilterData filterData;
	filterData.data.word0 = in.layerNumber;
	//filterData.data.word1 = m_collisionMatrix[in.layerNumber];
	//filterData.data.word2 = 1 << in.layerNumber;
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

	TouchRaycastQueryFilter queryFilter;
	
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
			if (blockHit.shape && blockHit.shape->userData != nullptr) {
				out.id = static_cast<CollisionData*>(hitBufferStruct.block.shape->userData)->thisId;
				ConvertVectorPxToDx(hitBufferStruct.block.position, out.blockPosition);
				ConvertVectorPxToDx(hitBufferStruct.block.normal, out.blockNormal);
			}
			else {
				out.hasBlock = false; // userData가 유효하지 않으면 블록 처리 안함
			}
		}

		unsigned int hitSize = hitBufferStruct.nbTouches;
		out.hitSize = hitSize;

		for (unsigned int hitNum = 0; hitNum < hitSize; hitNum++)
		{
			const physx::PxRaycastHit& hit = hitBufferStruct.touches[hitNum];
			physx::PxShape* shape = hit.shape;

			if (shape && shape->userData != nullptr) // 추가: shape 및 userData 유효성 확인
			{
				DirectX::SimpleMath::Vector3 position;
				DirectX::SimpleMath::Vector3 normal;
				ConvertVectorPxToDx(hit.position, position);
				ConvertVectorPxToDx(hit.normal, normal);
				unsigned int id = static_cast<CollisionData*>(shape->userData)->thisId;
				unsigned int layerNumber = static_cast<CollisionData*>(shape->userData)->thisLayerNumber;

				out.contectPoints.push_back(position);
				out.contectNormals.push_back(normal);
				out.hitLayerNumber.push_back(layerNumber);
				out.hitId.push_back(id);
			}
			else {
				out.hitSize--; // 유효하지 않은 hit는 카운트에서 제외
			}
		}
	}
	
	return out;
}

// 단일 레이캐스트, static dynamic 모두 검사, 
RayCastOutput PhysicX::Raycast(const RayCastInput& in)
{
	physx::PxVec3 pxOrgin;
	physx::PxVec3 pxDirection;
	ConvertVectorDxToPx(in.origin, pxOrgin);
	ConvertVectorDxToPx(in.direction, pxDirection);

	// RaycastHit 
	physx::PxRaycastBuffer hitBufferStruct;

	//충돌 필터 데이터
	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC 
		| physx::PxQueryFlag::eDYNAMIC
		| physx::PxQueryFlag::ePREFILTER
		| physx::PxQueryFlag::eDISABLE_HARDCODED_FILTER;
	const unsigned int ALL_LAYER = ~0; // 모든 레이어를 의미하는 값

	if (in.layerNumber == ALL_LAYER) {
		filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어를 의미하는 값
		filterData.data.word1 = 0xFFFFFFFF;
		filterData.data.word2 = 0xFFFFFFFF;
	}
	else {
		// 특정 레이어에 대해서만 raycast
		filterData.data.word0 = in.layerNumber;
		//filterData.data.word1 = m_collisionMatrix[in.layerNumber];
		//filterData.data.word2 = 1 << in.layerNumber;
	}

	BlockRaycastQueryFilter queryFilter;

	bool isAnyHit;

	isAnyHit = m_scene->raycast(pxOrgin, pxDirection, in.distance, hitBufferStruct, physx::PxHitFlag::eDEFAULT, filterData, &queryFilter);

	RayCastOutput out;

	//hit가 있는 경우
	if (isAnyHit)
	{
		out.hasBlock = hitBufferStruct.hasBlock;
		if (out.hasBlock)
		{
			const physx::PxRaycastHit& blockHit = hitBufferStruct.block;
			if (blockHit.shape && blockHit.shape->userData != nullptr) {
				out.id = static_cast<CollisionData*>(hitBufferStruct.block.shape->userData)->thisId;
				out.blockLayerNumber = static_cast<CollisionData*>(hitBufferStruct.block.shape->userData)->thisLayerNumber;
				ConvertVectorPxToDx(hitBufferStruct.block.position, out.blockPosition);
				ConvertVectorPxToDx(hitBufferStruct.block.normal, out.blockNormal);
			}
			else {
				out.hasBlock = false;
				std::cout << "Not physx block userdata" << std::endl;
			}
		}
	}

	return out;
}

//PxQueryFlag::eNO_BLOCK 활성화시 block 처리x
RayCastOutput PhysicX::RaycastAll(const RayCastInput& in)
{
	physx::PxVec3 pxOrgin;
	physx::PxVec3 pxDirection;
	ConvertVectorDxToPx(in.origin, pxOrgin);
	ConvertVectorDxToPx(in.direction, pxDirection);

	// RaycastHit 
	const physx::PxU32 maxHits = 20;
	physx::PxRaycastHit hitBuffer[maxHits];
	physx::PxRaycastBuffer hitBufferStruct(hitBuffer, maxHits);

	//충돌 필터 데이터
	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC
		| physx::PxQueryFlag::eDYNAMIC
		| physx::PxQueryFlag::ePREFILTER
		| physx::PxQueryFlag::eNO_BLOCK
		| physx::PxQueryFlag::eDISABLE_HARDCODED_FILTER;

	const unsigned int ALL_LAYER = ~0; // 모든 레이어를 의미하는 값

	if (in.layerNumber == ALL_LAYER) {
		filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어를 의미하는 값
		filterData.data.word1 = 0xFFFFFFFF;
		filterData.data.word2 = 0xFFFFFFFF;
	}
	else {
		// 특정 레이어에 대해서만 raycast
		filterData.data.word0 = in.layerNumber;
		//filterData.data.word1 = m_collisionMatrix[in.layerNumber];
		//filterData.data.word2 = 1 << in.layerNumber;
	}

	

	

	TouchRaycastQueryFilter queryFilter;

	bool isAnyHit;

	isAnyHit = m_scene->raycast(pxOrgin, pxDirection, in.distance, hitBufferStruct, physx::PxHitFlag::eDEFAULT, filterData, &queryFilter);


	RayCastOutput out;

	//hit가 있는 경우
	if (isAnyHit)
	{
		unsigned int hitSize = hitBufferStruct.nbTouches;
		out.hitSize = hitSize;

		for (unsigned int hitNum = 0; hitNum < hitSize; hitNum++)
		{
			const physx::PxRaycastHit& hit = hitBufferStruct.touches[hitNum];
			physx::PxShape* shape = hit.shape;

			if (shape && shape->userData != nullptr) { // 추가: shape 및 userData 유효성 확인
			DirectX::SimpleMath::Vector3 position;
			DirectX::SimpleMath::Vector3 normal;
			ConvertVectorPxToDx(hit.position, position);
			ConvertVectorPxToDx(hit.normal, normal);
			unsigned int id = static_cast<CollisionData*>(shape->userData)->thisId;
			unsigned int layerNumber = static_cast<CollisionData*>(shape->userData)->thisLayerNumber;

			out.contectPoints.push_back(position);
			out.contectNormals.push_back(normal);
			out.hitLayerNumber.push_back(layerNumber);
			out.hitId.push_back(id);
			}
			else {
				out.hitSize--; // 유효하지 않은 hit는 카운트에서 제외
			}
		}
	}

	return out;
}
//==========================================================================================
//rigid body

void PhysicX::CreateStaticBody(const BoxColliderInfo & info, const EColliderType & colliderType)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material, true);

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
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material, true);

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
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material, true);

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
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material, true);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);
	shape->release();
}

void PhysicX::CreateStaticBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices,info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material, true);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);

	shape->release();	
}

void PhysicX::CreateStaticBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols,info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField,physx::PxMeshGeometryFlag::eDOUBLE_SIDED), *material, true);

	StaticRigidBody* staticBody = SettingStaticBody(shape, info.colliderInfo, colliderType, m_collisionMatrix);
	
	DirectX::SimpleMath::Vector3 offsetPosition( info.rowScale * info.numRows * 0.5f,0.0f,- info.colScale * info.numCols * 0.5f);
	const float pi = 3.1415926535f;
	shape->release();
	DirectX::SimpleMath::Quaternion offsetRotation = DirectX::SimpleMath::Quaternion::CreateFromAxisAngle(DirectX::SimpleMath::Vector3::UnitZ, pi);
	staticBody->SetOffsetPosition(offsetPosition);
	staticBody->SetOffsetRotation(offsetRotation);
}

void PhysicX::CreateDynamicBody(const BoxColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxBoxGeometry(info.boxExtent.x, info.boxExtent.y, info.boxExtent.z), *material, true);

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
	physx::PxShape* shape = m_physics->createShape(PxSphereGeometry(info.radius), *material, true);
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
	physx::PxShape* shape = m_physics->createShape(PxCapsuleGeometry(info.radius, info.height), *material, true);
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
	physx::PxShape* shape = m_physics->createShape(PxConvexMeshGeometry(pxConvexMesh), *material, true);

	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const TriangleMeshColliderInfo & info, const EColliderType & colliderType, bool isKinematic)
{
	TriangleMeshResource* triangleMesh = new TriangleMeshResource(m_physics, info.vertices, info.vertexSize, info.indices, info.indexSize);
	physx::PxTriangleMesh* pxTriangleMesh = triangleMesh->GetTriangleMesh();

	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxTriangleMeshGeometry(pxTriangleMesh), *material, true);

	shape->setFlag(physx::PxShapeFlag::eSCENE_QUERY_SHAPE,true);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

void PhysicX::CreateDynamicBody(const HeightFieldColliderInfo & info, const EColliderType & colliderType,  bool isKinematic)
{
	HeightFieldResource* heightField = new HeightFieldResource(m_physics, info.heightMep, info.numCols, info.numRows);
	physx::PxHeightField* pxHeightField = heightField->GetHeightField();
	physx::PxMaterial* material = m_physics->createMaterial(info.colliderInfo.staticFriction, info.colliderInfo.dynamicFriction, info.colliderInfo.restitution);
	physx::PxShape* shape = m_physics->createShape(PxHeightFieldGeometry(pxHeightField), *material, true);
	DynamicRigidBody* dynamicBody = SettingDynamicBody(shape, info.colliderInfo, colliderType, m_collisionMatrix, isKinematic);
	shape->release();
}

StaticRigidBody* PhysicX::SettingStaticBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, unsigned int* collisionMatrix)
{
	//filterData
	physx::PxFilterData filterData;
	filterData.word0 = colInfo.layerNumber;
	filterData.word1 = collisionMatrix[colInfo.layerNumber];
	filterData.word2 = 1 << colInfo.layerNumber;
	//filterData.word1 = 0xFFFFFFFF;
	shape->setSimulationFilterData(filterData);
	shape->setQueryFilterData(filterData);
	//collisionData
	StaticRigidBody* staticBody = new StaticRigidBody(collideType,colInfo.id,colInfo.layerNumber);
	CollisionData* collisionData = new CollisionData();
	if (collisionData) // 추가: collisionData 유효성 확인
	{
		//스테틱 바디 초기화-->rigidbody 생성 및 shape attach , collider 정보 등록, collisionData 등록
		if (!staticBody->Initialize(colInfo,shape,m_physics,collisionData))
		{
			Debug->LogError("PhysicX::SettingStaticBody() : staticBody Initialize failed id :" + std::to_string(colInfo.id));
			delete collisionData; // 실패 시 메모리 해제
			return nullptr;
		}

		//충돌데이터 등록, 물리 바디 등록
		m_collisionDataContainer.insert(std::make_pair(colInfo.id, collisionData));
		m_rigidBodyContainer.insert(std::make_pair(staticBody->GetID(), staticBody));

		m_updateActors.push_back(staticBody);
	}
	else
	{
		Debug->LogError("PhysicX::SettingStaticBody() : Failed to allocate CollisionData for id " + std::to_string(colInfo.id));
		delete staticBody; // staticBody도 해제
		return nullptr;
	}
	return staticBody;
}

DynamicRigidBody* PhysicX::SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, unsigned int* collisionMatrix, bool isKinematic)
{
	//필터데이터
	physx::PxFilterData filterData;
	filterData.word0 = colInfo.layerNumber;
	filterData.word1 = collisionMatrix[colInfo.layerNumber];
	filterData.word2 = 1 << colInfo.layerNumber;
	//filterData.word1 = 0xFFFFFFFF;
	shape->setSimulationFilterData(filterData);
	shape->setQueryFilterData(filterData);
	//collisionData
	DynamicRigidBody* dynamicBody = new DynamicRigidBody(collideType, colInfo.id, colInfo.layerNumber);
	CollisionData* collisionData = new CollisionData();
	if (collisionData) // 추가: collisionData 유효성 확인
	{
		//다이나믹 바디 초기화-->rigidbody 생성 및 shape attach , collider 정보 등록, collisionData 등록
		if (!dynamicBody->Initialize(colInfo, shape, m_physics, collisionData,isKinematic))
		{
			Debug->LogError("PhysicX::SettingDynamicBody() : dynamicBody Initialize failed id :" + std::to_string(colInfo.id));
			delete collisionData; // 실패 시 메모리 해제
			return nullptr;
		}

		//충돌데이터 등록, 물리 바디 등록
		m_collisionDataContainer.insert(std::make_pair(colInfo.id, collisionData));
		m_rigidBodyContainer.insert(std::make_pair(dynamicBody->GetID(), dynamicBody));

		m_updateActors.push_back(dynamicBody);
	}
	else
	{
		Debug->LogError("PhysicX::SettingDynamicBody() : Failed to allocate CollisionData for id " + std::to_string(colInfo.id));
		delete dynamicBody; // dynamicBody도 해제
		return nullptr;
	}
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

	auto it = m_rigidBodyContainer.find(id);
	if (it == m_rigidBodyContainer.end())
	{
		//Debug->LogError("PhysicX::GetRigidBodyData: RigidBody with ID " + std::to_string(id) + " not found in container. This might indicate a lifecycle management issue.");
		return rigidBodyData;
	}
	auto body = it->second;
	
	//dynamicBody 인 경우
	DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
	if (dynamicBody)
	{
		physx::PxRigidDynamic* pxBody = dynamicBody->GetRigidDynamic();
		
		physx::PxTransform pxTransform = pxBody->getGlobalPose();
		DirectX::SimpleMath::Vector3 position;
		DirectX::SimpleMath::Quaternion rotation;
		
		DirectX::SimpleMath::Vector3 scale = dynamicBody->GetScale();
		DirectX::SimpleMath::Vector3 offsetPos = dynamicBody->GetOffsetPosition();//ridid body의 오프셋 위치
		DirectX::SimpleMath::Quaternion offsetRotate = dynamicBody->GetOffsetRotation();//ridid body의 오프셋 회전

		ConvertVectorPxToDx(pxTransform.p, position);
		ConvertQuaternionPxToDx(pxTransform.q, rotation);

		DirectX::SimpleMath::Quaternion finalrotation = rotation * offsetRotate; //오프셋 회전 적용
		DirectX::SimpleMath::Vector3 rotatedOffsetPos = DirectX::SimpleMath::Vector3::Transform(offsetPos,rotation);

		DirectX::SimpleMath::Vector3 finalPosition = position + rotatedOffsetPos; //오프셋 위치 적용
		
		rigidBodyData.transform =	DirectX::SimpleMath::Matrix::CreateScale(scale) *
									DirectX::SimpleMath::Matrix::CreateFromQuaternion(finalrotation) *
									DirectX::SimpleMath::Matrix::CreateTranslation(finalPosition);
		
		ConvertVectorPxToDx(pxBody->getLinearVelocity(), rigidBodyData.linearVelocity);
		DirectX::SimpleMath::Vector3& velocity = rigidBodyData.linearVelocity;

		if (std::abs(velocity.x) < 0.01f)
		{
			velocity.x = 0.0f;
		}

		if (std::abs(velocity.z) < 0.01f)
		{
			velocity.z = 0.0f;
		}
		ConvertVectorPxToDx(pxBody->getAngularVelocity(), rigidBodyData.angularVelocity);

		physx::PxRigidDynamicLockFlags flags = pxBody->getRigidDynamicLockFlags();

		rigidBodyData.isLockLinearX = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X);
		rigidBodyData.isLockLinearY = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y);
		rigidBodyData.isLockLinearZ = (flags & physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z);
		rigidBodyData.isLockAngularX = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X);
		rigidBodyData.isLockAngularY = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y);
		rigidBodyData.isLockAngularZ = (flags & physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z);

		rigidBodyData.isKinematic = pxBody->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC;
		rigidBodyData.isDisabled = pxBody->getActorFlags() & physx::PxActorFlag::eDISABLE_SIMULATION;
		rigidBodyData.useGravity = !(pxBody->getActorFlags() & physx::PxActorFlag::eDISABLE_GRAVITY);

		const physx::PxU32 numShapes = pxBody->getNbShapes();
		if (numShapes > 0)
		{
			std::vector<physx::PxShape*> shapes(numShapes);
			pxBody->getShapes(shapes.data(), numShapes);
			if (shapes[0])
			{
				rigidBodyData.m_EColliderType = (shapes[0]->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE) ? EColliderType::TRIGGER : EColliderType::COLLISION;
				rigidBodyData.isColliderEnabled = (shapes[0]->getFlags() & physx::PxShapeFlag::eSIMULATION_SHAPE) || (shapes[0]->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE);
			}
		}
	}
	
	//staticBody 인 경우
	StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
	if (staticBody)
	{
		physx::PxRigidStatic* pxBody = staticBody->GetRigidStatic();
		physx::PxTransform pxTransform = pxBody->getGlobalPose();
		DirectX::SimpleMath::Vector3 position;
		DirectX::SimpleMath::Quaternion rotation;

		DirectX::SimpleMath::Vector3 scale = staticBody->GetScale();
		DirectX::SimpleMath::Vector3 offsetPos = staticBody->GetOffsetPosition();//ridid body의 오프셋 위치
		DirectX::SimpleMath::Quaternion offsetRotate = staticBody->GetOffsetRotation();//ridid body의 오프셋 회전

		ConvertVectorPxToDx(pxTransform.p, position);
		ConvertQuaternionPxToDx(pxTransform.q, rotation);

		DirectX::SimpleMath::Quaternion finalrotation = rotation * offsetRotate; //오프셋 회전 적용
		DirectX::SimpleMath::Vector3 rotatedOffsetPos = DirectX::SimpleMath::Vector3::Transform(offsetPos, rotation);

		DirectX::SimpleMath::Vector3 finalPosition = position + rotatedOffsetPos; //오프셋 위치 적용
		rigidBodyData.transform =	DirectX::SimpleMath::Matrix::CreateScale(scale) * 
									DirectX::SimpleMath::Matrix::CreateFromQuaternion(finalrotation) * 
									DirectX::SimpleMath::Matrix::CreateTranslation(finalPosition);
	}

	return rigidBodyData;
}

RigidBody* PhysicX::GetRigidBody(const unsigned int& id)
{
	//등록되어 있는지 검색
	if (m_rigidBodyContainer.find(id) == m_rigidBodyContainer.end())
	{
		//Debug->LogWarning("GetRigidBody id :" + std::to_string(id) + " Get Failed");
		return nullptr;
	}
	RigidBody* body = m_rigidBodyContainer[id];
	if (body)
	{
		return body;
	}
	else
	{
		//Debug->LogWarning("GetRigidBody id :" + std::to_string(id) + " Get Failed");
		return nullptr;
	}
}

void PhysicX::SetRigidBodyData(const unsigned int& id, RigidBodyGetSetData& rigidBodyData)
{
	//데이터를 설정할 물리 바디 등록되어 있는지 검색
	if (m_rigidBodyContainer.find(id) == m_rigidBodyContainer.end())
	{
		std::cout << "PhysicX::SetRigidBodyData: RigidBody with ID " << id << " not found in container." << std::endl;
		return;
	}
	auto body = m_rigidBodyContainer.find(id)->second;


	//staticBody 인 경우
	StaticRigidBody* staticBody = dynamic_cast<StaticRigidBody*>(body);
	if (staticBody)
	{
		physx::PxRigidStatic* pxBody = staticBody->GetRigidStatic();
		DirectX::SimpleMath::Matrix dxMatrix = rigidBodyData.transform;
		physx::PxTransform pxPrevTransform = pxBody->getGlobalPose();

		physx::PxTransform pxTransform;
		DirectX::SimpleMath::Vector3 position;
		DirectX::SimpleMath::Vector3 scale;
		DirectX::SimpleMath::Quaternion rotation;
		dxMatrix.Decompose(scale, rotation, position);

		DirectX::SimpleMath::Vector3 offPos = staticBody->GetOffsetPosition();
		DirectX::SimpleMath::Quaternion offRot = staticBody->GetOffsetRotation();

		DirectX::SimpleMath::Quaternion invOffsetRot;
		offRot.Inverse(invOffsetRot);
		DirectX::SimpleMath::Quaternion bodyRotation = rotation * invOffsetRot;

		DirectX::SimpleMath::Vector3 rotatedOffsetPos = DirectX::SimpleMath::Vector3::Transform(offPos, bodyRotation);
		DirectX::SimpleMath::Vector3 bodyPosition = position - rotatedOffsetPos;

		ConvertVectorDxToPx(bodyPosition, pxTransform.p);
		ConvertQuaternionDxToPx(bodyRotation, pxTransform.q);

		//CopyMatrixDxToPx(dxMatrix, pxTransform);

		if (IsTransformDifferent(pxPrevTransform, pxTransform)) {
			pxBody->setGlobalPose(pxTransform);
		}
		return;
	}

	//dynamicBody 인 경우
	DynamicRigidBody* dynamicBody = dynamic_cast<DynamicRigidBody*>(body);
	if (dynamicBody)
	{
		physx::PxRigidDynamic* pxBody = dynamicBody->GetRigidDynamic();
		if (!pxBody) return;
		// Actor 및 Body 플래그 설정
		pxBody->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !rigidBodyData.useGravity);
		pxBody->setActorFlag(physx::PxActorFlag::eDISABLE_SIMULATION, rigidBodyData.isDisabled);
		pxBody->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, rigidBodyData.isKinematic);

		// Shape 플래그 설정
		const PxU32 numShapes = pxBody->getNbShapes();
		std::vector<physx::PxShape*> shapes(numShapes);
		pxBody->getShapes(shapes.data(), numShapes);
		for (physx::PxShape* shape : shapes)
		{
			if (rigidBodyData.isColliderEnabled)
			{
				bool isTrigger = (rigidBodyData.m_EColliderType == EColliderType::TRIGGER);
				shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, !isTrigger);
				shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, isTrigger);
			}
			else
			{
				shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
				shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, false);
			}
		}

		// 속도 및 기타 데이터 설정
		if (!rigidBodyData.isKinematic)
		{
			physx::PxVec3 pxLinearVelocity;
			physx::PxVec3 pxAngularVelocity;
			ConvertVectorDxToPx(rigidBodyData.linearVelocity, pxLinearVelocity);
			ConvertVectorDxToPx(rigidBodyData.angularVelocity, pxAngularVelocity);
			pxBody->setLinearVelocity(pxLinearVelocity);
			pxBody->setAngularVelocity(pxAngularVelocity);
		}

		if (rigidBodyData.forceMode != 4) 
		{
			PxVec3 velocity;
			ConvertVectorDxToPx(rigidBodyData.velocity, velocity);
			pxBody->addForce(velocity, static_cast<physx::PxForceMode::Enum>(rigidBodyData.forceMode));
			rigidBodyData.forceMode = 4;
		}

		if (rigidBodyData.isDirty) {
			pxBody->setMaxLinearVelocity(rigidBodyData.maxLinearVelocity);
			pxBody->setMaxAngularVelocity(rigidBodyData.maxAngularVelocity);
			pxBody->setMaxContactImpulse(rigidBodyData.maxContactImpulse);
			pxBody->setMaxDepenetrationVelocity(rigidBodyData.maxDepenetrationVelocity);

			pxBody->setAngularDamping(rigidBodyData.AngularDamping);
			pxBody->setLinearDamping(rigidBodyData.LinearDamping);
			pxBody->setMass(rigidBodyData.mass);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X, rigidBodyData.isLockAngularX);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y, rigidBodyData.isLockAngularY);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z, rigidBodyData.isLockAngularZ);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X, rigidBodyData.isLockLinearX);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y, rigidBodyData.isLockLinearY);
			pxBody->setRigidDynamicLockFlag(physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z, rigidBodyData.isLockLinearZ);
		}

		if (rigidBodyData.moveDirty) {
			pxBody->setKinematicTarget(
				PxTransform(PxVec3(
						rigidBodyData.movePosition.x, 
						rigidBodyData.movePosition.y, 
						rigidBodyData.movePosition.z))
			);
		}
		else {
			DirectX::SimpleMath::Matrix dxMatrix = rigidBodyData.transform;
			physx::PxTransform pxTransform;
			DirectX::SimpleMath::Vector3 position;
			DirectX::SimpleMath::Vector3 scale;
			DirectX::SimpleMath::Quaternion rotation;
			dxMatrix.Decompose(scale, rotation, position);

			DirectX::SimpleMath::Vector3 offPos = dynamicBody->GetOffsetPosition();
			DirectX::SimpleMath::Quaternion offRot = dynamicBody->GetOffsetRotation();

			DirectX::SimpleMath::Quaternion invOffsetRot;
			offRot.Inverse(invOffsetRot);
			DirectX::SimpleMath::Quaternion bodyRotation = rotation * invOffsetRot;

			DirectX::SimpleMath::Vector3 rotatedOffsetPos = DirectX::SimpleMath::Vector3::Transform(offPos, bodyRotation);
			DirectX::SimpleMath::Vector3 bodyPosition = position - rotatedOffsetPos;

			ConvertVectorDxToPx(bodyPosition, pxTransform.p);
			ConvertQuaternionDxToPx(bodyRotation, pxTransform.q);

			//CopyMatrixDxToPx(dxMatrix, pxTransform);
			physx::PxTransform pxPrevTransform = pxBody->getGlobalPose();
			/*if (IsTransformDifferent(pxPrevTransform, pxTransform)) {
				pxBody->setGlobalPose(pxTransform);
			}*/
			pxBody->setGlobalPose(pxTransform);
			if (scale.x > 0.0f && scale.y > 0.0f && scale.z > 0.0f)
			{
				dynamicBody->SetConvertScale(scale, m_physics, m_collisionMatrix);
			}
			else {
				Debug->LogError("PhysicX::SetRigidBodyData() : scale is 0.0f id :" + std::to_string(id));
			}
		}
		dynamicBody->ChangeLayerNumber(rigidBodyData.LayerNumber, m_collisionMatrix);
	}
}

void PhysicX::RemoveRigidBody(const unsigned int& id, physx::PxScene* scene, std::vector<physx::PxActor*>& removeActorList)
{
	//등록되어 있는지 검색
	if (m_rigidBodyContainer.find(id)==m_rigidBodyContainer.end())
	{
		Debug->LogWarning("RemoveRigidBody id :" + std::to_string(id) + " Remove Failed");
		return;
	}

	RigidBody* body = m_rigidBodyContainer[id];

	//씬에서 제거할 바디인 경우 (PxActor를 직접 씬에서 제거)
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

	//업데이트 리스트에서 해당 바디를 제거
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
	//씬에서 제거할 바디인 경우 (컨테이너에 등록된 모든 바디를 씬에서 제거)
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
	//컨테이너의 모든 바디 제거 
	m_rigidBodyContainer.clear();

	//업데이트 리스트의 모든 바디 제거
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
	//등록된 모든 바디 제거
	m_updateActors.clear();
}

//==============================================
//캐릭터 컨트롤러

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
	m_characterControllerManager->purgeControllers();
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

void PhysicX::SetCharacterMovementMaxSpeed(const CharactorControllerInputInfo& info,float maxSpeed)
{
	if (m_characterControllerContainer.find(info.id) == m_characterControllerContainer.end())
	{
		return;
	}

	CharacterController* controller = m_characterControllerContainer[info.id];
	controller->GetCharacterMovement()->SetMaxSpeed(maxSpeed);

}

void PhysicX::SetVelocity(const CharactorControllerInputInfo& info, DirectX::SimpleMath::Vector3 velocity)
{
	if (m_characterControllerContainer.find(info.id) == m_characterControllerContainer.end())
	{
		return;
	}

	CharacterController* controller = m_characterControllerContainer[info.id];
	controller->GetCharacterMovement()->SetVelocity(velocity);
}

void PhysicX::SetKnockBack(const CharactorControllerInputInfo& info,bool _isknockback, DirectX::SimpleMath::Vector3 velocity)
{

	if (m_characterControllerContainer.find(info.id) == m_characterControllerContainer.end())
	{
		return;
	}

	CharacterController* controller = m_characterControllerContainer[info.id];
	controller->GetCharacterMovement()->SetVelocity({0,0,0});
	controller->GetCharacterMovement()->SetKnockback(_isknockback);
	controller->GetCharacterMovement()->SetKnockbackVeloicy(velocity);
}



void PhysicX::SetControllerPosition(UINT id, const DirectX::SimpleMath::Vector3& pos)
{
	// 1. 컨테이너에서 ID를 기반으로 CharacterController 래퍼 클래스를 찾습니다.
	auto it = m_characterControllerContainer.find(id);
	if (it == m_characterControllerContainer.end())
	{
		Debug->LogError("PhysicX::SetControllerPosition() : CharacterController wrapper for ID not found: " + std::to_string(id));
		return;
	}

	CharacterController* controllerWrapper = it->second;
	if (!controllerWrapper)
	{
		Debug->LogError("PhysicX::SetControllerPosition() : CharacterController wrapper is null for ID: " + std::to_string(id));
		return;
	}

	// 2. 래퍼 클래스에서 실제 physx::PxController 포인터를 가져옵니다.
	// (CharacterController 클래스에 GetController() 함수가 구현되어 있어야 합니다.)
	physx::PxController* pxController = controllerWrapper->GetController();
	if (!pxController)
	{
		Debug->LogError("PhysicX::SetControllerPosition() : Raw PxController is null for ID: " + std::to_string(id));
		return;
	}

	// 3. PhysX 컨트롤러의 위치를 강제로 설정합니다.
	pxController->setPosition(physx::PxExtendedVec3(pos.x, pos.y, pos.z));
}

CharacterController* PhysicX::GetCCT(const unsigned int& id)
{
	if (m_characterControllerContainer.find(id) != m_characterControllerContainer.end())
	{
		return m_characterControllerContainer[id];
	}
	else
	{
		return nullptr;
	}
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
//캐릭터 물리 제어 -> 래그돌

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
	DirectX::SimpleMath::Matrix prevDxMatrix= ragdoll->GetWorldTransform();
	DirectX::SimpleMath::Matrix newDxMatrix = DirectX::SimpleMath::Matrix::Identity;


	DirectX::SimpleMath::Vector3 scale;
	DirectX::SimpleMath::Vector3 position;
	DirectX::SimpleMath::Quaternion rotation;
	prevDxMatrix.Decompose(scale, rotation, position);

	ConvertVectorPxToDx(pxTransform.p, position);
	ConvertQuaternionPxToDx(pxTransform.q, rotation);

	newDxMatrix = DirectX::SimpleMath::Matrix::CreateScale(scale) *
		DirectX::SimpleMath::Matrix::CreateFromQuaternion(rotation) *
		DirectX::SimpleMath::Matrix::CreateTranslation(position);

	

	data.WorldTransform = newDxMatrix;
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

		//래그돌 시뮬레이션을 해야하는 경우
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
	//auto isconnected = pvd->connect(*transport, PxPvdInstrumentationFlag::ePROFILE);
    std::cout << "pvd connected : " << isconnected << std::endl;
}

void PhysicX::CollisionDataUpdate()
{
	//컨테이너에서 제거
	for (auto& removeId : m_removeCollisionIds) {
		auto iter = m_collisionDataContainer.find(removeId);
		if (iter != m_collisionDataContainer.end())
		{
			m_collisionDataContainer.erase(iter);
		}
	}
	m_removeCollisionIds.clear();

	//충돌 데이터 리스트에서 제거
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
	//충돌 데이터가 등록되어 있는지 검색
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

	//convexMesh의 모든 인덱스 데이터 가져오기
	const physx::PxVec3* convexMeshVertices = convexMeshGeom.convexMesh->getVertices();
	const physx::PxU32 vertexCount = convexMeshGeom.convexMesh->getNbVertices();

	const physx::PxU8* convexMeshIndices = convexMeshGeom.convexMesh->getIndexBuffer();
	const physx::PxU32 polygonCount = convexMeshGeom.convexMesh->getNbPolygons();

	//폴리곤 개수만큼 polygon 벡터 생성
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

void PhysicX::DrawPVDLine(DirectX::SimpleMath::Vector3 ori, DirectX::SimpleMath::Vector3 end)
{
	PxPvdSceneClient* pvdClient = m_scene->getScenePvdClient();
	if (pvdClient && pvd->isConnected())
	{
		// 색상 설정
		const PxU32 green = PxDebugColor::eARGB_GREEN;
		const PxU32 red = PxDebugColor::eARGB_RED;

		// 충돌 여부에 따라 색상 변경
		PxU32 color = green;

		PxVec3 origin;
		PxVec3 endpoint;

		ConvertVectorDxToPx(ori, origin);
		ConvertVectorDxToPx(end, endpoint);

		// 선 하나를 그리는 것이므로 lineCount = 1
		PxDebugLine line(origin, endpoint, color);

		pvdClient->drawLines(&line, 1);
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

bool PhysicX::IsKinematic(unsigned int id) const
{
	auto it = m_rigidBodyContainer.find(id);
	if (it != m_rigidBodyContainer.end())
	{
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(it->second))
		{
			if (dynamicBody->GetRigidDynamic()) // 추가: GetRigidDynamic() 유효성 확인
			{
				return dynamicBody->GetRigidDynamic()->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC;
			}
		}
	}
	return false;
}

bool PhysicX::IsTrigger(unsigned int id) const
{
	auto it = m_rigidBodyContainer.find(id);
	if (it != m_rigidBodyContainer.end())
	{
		physx::PxRigidActor* actor = nullptr;
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(it->second)) actor = dynamicBody->GetRigidDynamic();
		else if (auto* staticBody = dynamic_cast<StaticRigidBody*>(it->second)) actor = staticBody->GetRigidStatic();

		if (actor)
		{
			const physx::PxU32 numShapes = actor->getNbShapes();
			if (numShapes > 0)
			{
				std::vector<physx::PxShape*> shapes(numShapes);
				actor->getShapes(shapes.data(), numShapes);
				if (shapes[0]) // 추가: shapes[0] 유효성 확인
				{
					return shapes[0]->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE;
				}
			}
		}
	}
	return false;
}

bool PhysicX::IsColliderEnabled(unsigned int id) const
{
	auto it = m_rigidBodyContainer.find(id);
	if (it != m_rigidBodyContainer.end())
	{
		physx::PxRigidActor* actor = nullptr;
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(it->second)) actor = dynamicBody->GetRigidDynamic();
		else if (auto* staticBody = dynamic_cast<StaticRigidBody*>(it->second)) actor = staticBody->GetRigidStatic();

		if (actor)
		{
			const physx::PxU32 numShapes = actor->getNbShapes();
			if (numShapes > 0)
			{
				std::vector<physx::PxShape*> shapes(numShapes);
				actor->getShapes(shapes.data(), numShapes);
				if (shapes[0]) // 추가: shapes[0] 유효성 확인
				{
					// eSIMULATION_SHAPE 또는 eTRIGGER_SHAPE가 설정되어 있으면 활성화된 상태로 간주
					return (shapes[0]->getFlags() & physx::PxShapeFlag::eSIMULATION_SHAPE) || (shapes[0]->getFlags() & physx::PxShapeFlag::eTRIGGER_SHAPE);
				}
			}
		}
	}
	return false;
}

bool PhysicX::IsUseGravity(unsigned int id) const
{
	auto it = m_rigidBodyContainer.find(id);
	if (it != m_rigidBodyContainer.end())
	{
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(it->second))
		{
			if (dynamicBody->GetRigidDynamic())
			{
				return !(dynamicBody->GetRigidDynamic()->getActorFlags() & physx::PxActorFlag::eDISABLE_GRAVITY);
			}
		}
	}
	return false; // 혹은 기본값
}


/**
 * @brief 지정된 박스(Box) 형태를 특정 경로로 스윕하여 충돌하는 객체를 찾습니다.
 * @param in 스윕 시작 위치, 방향, 거리 등의 입력 정보
 * @param boxExtent 박스의 절반 크기 (중심에서 각 면까지의 거리)
 * @return 스윕 결과를 담은 SweepOutput 구조체
 */
SweepOutput PhysicX::BoxSweep(const SweepInput& in, const DirectX::SimpleMath::Vector3& boxExtent)
{
	// --- 1. 입력 데이터를 PhysX가 사용하는 형태로 변환 ---
	// 엔진(오른손) 좌표계의 위치/회전/방향을 PhysX(왼손) 좌표계로 변환합니다.
	physx::PxTransform startPose;
	ConvertVectorDxToPx(in.startPosition, startPose.p);
	ConvertQuaternionDxToPx(in.startRotation, startPose.q);

	physx::PxVec3 unitDir;
	ConvertVectorDxToPx(in.direction, unitDir);
	unitDir.normalize(); // 방향 벡터는 항상 정규화해야 합니다.

	// --- 2. 스윕할 셰이프의 모양(Geometry)을 정의 ---
	physx::PxBoxGeometry boxGeometry(boxExtent.x, boxExtent.y, boxExtent.z);

	// --- 3. 결과를 받을 버퍼를 준비 ---
	const physx::PxU32 maxHits = 20; // 한 번의 스윕으로 감지할 수 있는 최대 객체 수
	physx::PxSweepHit hitBuffer[maxHits];
	physx::PxSweepBuffer sweepResult(hitBuffer, maxHits);

	// --- 4. 충돌 필터 설정 ---
	physx::PxQueryFilterData filterData;
	// eSTATIC, eDYNAMIC: 정적/동적 객체 모두와 충돌을 검사합니다.
	// ePREFILTER: 성능을 위해 사전 필터링을 사용합니다.
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	// 어떤 레이어와 충돌할지 비트마스크로 지정합니다.
	filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	filterData.data.word2 = 1 << in.layerMask;

	// --- 5. 스윕 실행 ---
	bool isHit = m_scene->sweep(
		boxGeometry,      // 스윕할 셰이프의 모양
		startPose,        // 시작 위치 및 회전
		unitDir,          // 스윕 방향
		in.distance,      // 스윕 거리
		sweepResult,      // 결과를 저장할 버퍼
		// eDEFAULT: 기본 충돌 처리.
		// eMESH_MULTIPLE: 삼각형 메쉬와 충돌 시, 여러 개의 삼각형과 충돌을 감지하도록 허용.
		physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMESH_MULTIPLE,
		filterData        // 위에서 설정한 충돌 필터
	);

	// --- 6. PhysX 결과(왼손)를 게임 로직이 사용할 형태(오른손)로 변환하여 반환 ---
	SweepOutput out;
	if (isHit)
	{
		out.hasBlock = sweepResult.hasBlock;
		for (physx::PxU32 i = 0; i < sweepResult.nbTouches; ++i)
		{
			const physx::PxSweepHit& hit = sweepResult.touches[i];
			// 충돌한 액터나 유저 데이터가 없으면 유효하지 않은 충돌이므로 건너뜁니다.
			if (!hit.actor || !hit.actor->userData) continue;

			SweepHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;
			hitResult.distance = hit.distance;

			// 충돌 지점과 법선 벡터도 왼손 좌표계에서 오른손 좌표계로 변환합니다.
			ConvertVectorPxToDx(hit.position, hitResult.hitPoint);
			ConvertVectorPxToDx(hit.normal, hitResult.hitNormal);

			out.touches.push_back(hitResult);
		}
	}

	return out;
}

/**
 * @brief 지정된 구(Sphere) 형태를 특정 경로로 스윕하여 충돌하는 객체를 찾습니다.
 * @param in 스윕 시작 위치, 방향, 거리 등의 입력 정보
 * @param radius 구의 반지름
 * @return 스윕 결과를 담은 SweepOutput 구조체
 */
SweepOutput PhysicX::SphereSweep(const SweepInput& in, float radius)
{
	// BoxSweep과 거의 동일하며, Geometry 정의 부분만 다릅니다.
	physx::PxTransform startPose;
	ConvertVectorDxToPx(in.startPosition, startPose.p);
	ConvertQuaternionDxToPx(in.startRotation, startPose.q);

	physx::PxVec3 unitDir;
	ConvertVectorDxToPx(in.direction, unitDir);
	unitDir.normalize();

	// 스윕할 셰이프를 구(Sphere)로 정의합니다.
	physx::PxSphereGeometry sphereGeometry(radius);

	const physx::PxU32 maxHits = 20;
	physx::PxSweepHit hitBuffer[maxHits];
	physx::PxSweepBuffer sweepResult(hitBuffer, maxHits);

	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	filterData.data.word2 = 1 << in.layerMask;

	bool isHit = m_scene->sweep(sphereGeometry, startPose, unitDir, in.distance, sweepResult, physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMESH_MULTIPLE, filterData);

	SweepOutput out;
	if (isHit)
	{
		out.hasBlock = sweepResult.hasBlock;
		for (physx::PxU32 i = 0; i < sweepResult.nbTouches; ++i)
		{
			const physx::PxSweepHit& hit = sweepResult.touches[i];
			if (!hit.actor || !hit.actor->userData) continue;

			SweepHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;
			hitResult.distance = hit.distance;

			ConvertVectorPxToDx(hit.position, hitResult.hitPoint);
			ConvertVectorPxToDx(hit.normal, hitResult.hitNormal);

			out.touches.push_back(hitResult);
		}
	}

	return out;
}

/**
 * @brief 지정된 캡슐(Capsule) 형태를 특정 경로로 스윕하여 충돌하는 객체를 찾습니다.
 * @param in 스윕 시작 위치, 방향, 거리 등의 입력 정보
 * @param radius 캡슐의 반지름
 * @param halfHeight 캡슐의 반쪽 높이 (중심에서 한쪽 끝까지의 거리)
 * @return 스윕 결과를 담은 SweepOutput 구조체
 */
SweepOutput PhysicX::CapsuleSweep(const SweepInput& in, float radius, float halfHeight)
{
	// BoxSweep과 거의 동일하며, Geometry 정의 부분만 다릅니다.
	physx::PxTransform startPose;
	ConvertVectorDxToPx(in.startPosition, startPose.p);
	ConvertQuaternionDxToPx(in.startRotation, startPose.q);

	physx::PxVec3 unitDir;
	ConvertVectorDxToPx(in.direction, unitDir);
	unitDir.normalize();

	// 스윕할 셰이프를 캡슐(Capsule)로 정의합니다.
	physx::PxCapsuleGeometry capsuleGeometry(radius, halfHeight);

	const physx::PxU32 maxHits = 20;
	physx::PxSweepHit hitBuffer[maxHits];
	physx::PxSweepBuffer sweepResult(hitBuffer, maxHits);

	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	filterData.data.word2 = 1 << in.layerMask;

	bool isHit = m_scene->sweep(capsuleGeometry, startPose, unitDir, in.distance, sweepResult, physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMESH_MULTIPLE, filterData);

	SweepOutput out;
	if (isHit)
	{
		out.hasBlock = sweepResult.hasBlock;
		for (physx::PxU32 i = 0; i < sweepResult.nbTouches; ++i)
		{
			const physx::PxSweepHit& hit = sweepResult.touches[i];
			if (!hit.actor || !hit.actor->userData) continue;

			SweepHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;
			hitResult.distance = hit.distance;

			ConvertVectorPxToDx(hit.position, hitResult.hitPoint);
			ConvertVectorPxToDx(hit.normal, hitResult.hitNormal);

			out.touches.push_back(hitResult);
		}
	}

	return out.touches.empty() ? SweepOutput{} : out;
}


/**
 * @brief 지정된 위치에 박스(Box) 형태의 영역을 생성하여 겹치는 모든 객체를 찾습니다.
 * @param in 오버랩 영역의 위치, 회전 등 입력 정보
 * @param boxExtent 박스의 절반 크기 (중심에서 각 면까지의 거리)
 * @return 오버랩 결과를 담은 OverlapOutput 구조체
 */
OverlapOutput PhysicX::BoxOverlap(const OverlapInput& in, const DirectX::SimpleMath::Vector3& boxExtent)
{
	// --- 1. 입력 데이터를 PhysX가 사용하는 형태로 변환 ---
	// 엔진(오른손) 좌표계의 위치/회전을 PhysX(왼손) 좌표계로 변환합니다.
	physx::PxTransform pose;
	ConvertVectorDxToPx(in.position, pose.p);
	ConvertQuaternionDxToPx(in.rotation, pose.q);

	// --- 2. 오버랩할 셰이프의 모양(Geometry)을 정의 ---
	physx::PxBoxGeometry boxGeometry(boxExtent.x, boxExtent.y, boxExtent.z);

	// --- 3. 결과를 받을 버퍼를 준비 ---
	const physx::PxU32 maxHits = 30; // 한 번의 오버랩으로 감지할 수 있는 최대 객체 수
	physx::PxOverlapHit hitBuffer[maxHits];
	physx::PxOverlapBuffer overlapResult(hitBuffer, maxHits);

	TouchRaycastQueryFilter filter;
	// --- 4. 충돌 필터 설정 ---
	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	filterData.data.word2 = 1 << in.layerMask;

	// --- 5. 오버랩 실행 ---
	bool isHit = m_scene->overlap(
		boxGeometry,    // 오버랩할 셰이프의 모양
		pose,           // 영역의 위치 및 회전
		overlapResult,  // 결과를 저장할 버퍼
		filterData,      // 위에서 설정한 충돌 필터
		&filter
	);

	// --- 6. PhysX 결과를 게임 로직이 사용할 형태로 변환하여 반환 ---
	OverlapOutput out;
	if (isHit)
	{
		for (physx::PxU32 i = 0; i < overlapResult.nbTouches; ++i)
		{
			const physx::PxOverlapHit& hit = overlapResult.touches[i];
			// 충돌한 액터나 유저 데이터가 없으면 유효하지 않은 충돌이므로 건너뜁니다.
			if (!hit.actor || !hit.actor->userData) continue;

			OverlapHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;

			out.touches.push_back(hitResult);
		}
	}

	return out;
}

/**
 * @brief 지정된 위치에 구(Sphere) 형태의 영역을 생성하여 겹치는 모든 객체를 찾습니다.
 * @param in 오버랩 영역의 위치, 회전 등 입력 정보
 * @param radius 구의 반지름
 * @return 오버랩 결과를 담은 OverlapOutput 구조체
 */
OverlapOutput PhysicX::SphereOverlap(const OverlapInput& in, float radius)
{
	// BoxOverlap과 거의 동일하며, Geometry 정의 부분만 다릅니다.
	physx::PxTransform pose;
	ConvertVectorDxToPx(in.position, pose.p);
	ConvertQuaternionDxToPx(in.rotation, pose.q);

	// 오버랩할 셰이프를 구(Sphere)로 정의합니다.
	physx::PxSphereGeometry sphereGeometry(radius);

	const physx::PxU32 maxHits = 30;
	physx::PxOverlapHit hitBuffer[maxHits];
	physx::PxOverlapBuffer overlapResult(hitBuffer, maxHits);

	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	//filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	//filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	const unsigned int ALL_LAYER = ~0; // 모든 레이어를 의미하는 값

	if (in.layerMask == ALL_LAYER) {
		filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어를 의미하는 값
		filterData.data.word1 = 0xFFFFFFFF;
		filterData.data.word2 = 0xFFFFFFFF;
	}
	else {
		// 특정 레이어에 대해서만 raycast
		filterData.data.word0 = in.layerMask;
		filterData.data.word1 = m_collisionMatrix[in.layerMask];
		filterData.data.word2 = 1 << in.layerMask;
	}

	bool isHit = m_scene->overlap(sphereGeometry, pose, overlapResult, filterData, m_touchCallback);

	OverlapOutput out;
	if (isHit)
	{
		for (physx::PxU32 i = 0; i < overlapResult.nbTouches; ++i)
		{
			const physx::PxOverlapHit& hit = overlapResult.touches[i];
			if (!hit.actor || !hit.actor->userData) continue;

			OverlapHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;

			out.touches.push_back(hitResult);
		}
	}

	return out;
}

/**
 * @brief 지정된 위치에 캡슐(Capsule) 형태의 영역을 생성하여 겹치는 모든 객체를 찾습니다.
 * @param in 오버랩 영역의 위치, 회전 등 입력 정보
 * @param radius 캡슐의 반지름
 * @param halfHeight 캡슐의 반쪽 높이 (중심에서 한쪽 끝까지의 거리)
 * @return 오버랩 결과를 담은 OverlapOutput 구조체
 */
OverlapOutput PhysicX::CapsuleOverlap(const OverlapInput& in, float radius, float halfHeight)
{
	// BoxOverlap과 거의 동일하며, Geometry 정의 부분만 다릅니다.
	physx::PxTransform pose;
	ConvertVectorDxToPx(in.position, pose.p);
	ConvertQuaternionDxToPx(in.rotation, pose.q);

	// 오버랩할 셰이프를 캡슐(Capsule)로 정의합니다.
	physx::PxCapsuleGeometry capsuleGeometry(radius, halfHeight);

	const physx::PxU32 maxHits = 30;
	physx::PxOverlapHit hitBuffer[maxHits];
	physx::PxOverlapBuffer overlapResult(hitBuffer, maxHits);

	physx::PxQueryFilterData filterData;
	filterData.flags = physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER;
	filterData.data.word0 = 0xFFFFFFFF; // 모든 레이어와 충돌하도록 설정합니다.
	filterData.data.word1 = in.layerMask; // 충돌할 레이어 마스크를 설정합니다. 
	filterData.data.word2 = 1 << in.layerMask;

	bool isHit = m_scene->overlap(capsuleGeometry, pose, overlapResult, filterData);

	OverlapOutput out;
	if (isHit)
	{
		for (physx::PxU32 i = 0; i < overlapResult.nbTouches; ++i)
		{
			const physx::PxOverlapHit& hit = overlapResult.touches[i];
			if (!hit.actor || !hit.actor->userData) continue;

			OverlapHitResult hitResult;
			CollisionData* userData = static_cast<CollisionData*>(hit.actor->userData);

			hitResult.hitObjectID = userData->thisId;
			hitResult.hitObjectLayer = userData->thisLayerNumber;

			out.touches.push_back(hitResult);
		}
	}

	return out;
}

void PhysicX::PutToSleep(unsigned int id)
{
	// 1. 전달받은 ID로 PxRigidDynamic 포인터를 찾습니다. (RigidBody 또는 CCT)
	physx::PxRigidDynamic* dynamicActor = nullptr;

	RigidBody* body = GetRigidBody(id);
	if (body)
	{
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(body))
		{
			dynamicActor = dynamicBody->GetRigidDynamic();
		}
	}
	else
	{
		CharacterController* cct = GetCCT(id);
		if (cct)
		{
			dynamicActor = cct->GetController()->getActor();
		}
	}

	// 2. 유효한 액터를 찾았는지, 그리고 씬에 속해있는지 확인합니다.
	if (dynamicActor && dynamicActor->getScene())
	{
		// 3. 가장 중요: 액터가 키네마틱이 아닌지 확인합니다.
		if (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
		{
			// 키네마틱 액터는 putToSleep()을 호출할 수 없으므로 아무것도 하지 않습니다.
			return;
		}

		// 4. 이미 잠들어있지 않다면 putToSleep()을 호출합니다.
		if (!dynamicActor->isSleeping())
		{
			dynamicActor->putToSleep();
		}
	}
}

void PhysicX::WakeUp(unsigned int id)
{
	// 1. 전달받은 ID로 PxRigidDynamic 포인터를 찾습니다. (RigidBody 또는 CCT)
	physx::PxRigidDynamic* dynamicActor = nullptr;

	RigidBody* body = GetRigidBody(id);
	if (body)
	{
		// RigidBody인 경우
		if (auto* dynamicBody = dynamic_cast<DynamicRigidBody*>(body))
		{
			dynamicActor = dynamicBody->GetRigidDynamic();
		}
	}
	else
	{
		// RigidBody가 아니면 CCT일 수 있습니다.
		CharacterController* cct = GetCCT(id);
		if (cct)
		{
			dynamicActor = cct->GetController()->getActor();
		}
	}

	// 2. 유효한 액터를 찾았는지, 그리고 씬에 속해있는지 확인합니다.
	if (dynamicActor && dynamicActor->getScene())
	{
		// 3. 가장 중요: 액터가 키네마틱이 아닌지 확인합니다.
		if (dynamicActor->getRigidBodyFlags() & physx::PxRigidBodyFlag::eKINEMATIC)
		{
			// 키네마틱 액터는 wakeUp()을 호출할 수 없으므로 아무것도 하지 않습니다.
			return;
		}

		// 4. 이미 깨어있지 않다면 wakeUp()을 호출합니다.
		if (dynamicActor->isSleeping())
		{
			dynamicActor->wakeUp();
		}
	}
}
