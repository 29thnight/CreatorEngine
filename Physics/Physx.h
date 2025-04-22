#pragma once
#include "../Utility_Framework/ClassProperty.h"
#include <directxtk/SimpleMath.h>
#include <physx/PxPhysics.h>
#include <physx/PxPhysicsAPI.h>
#include <physx/characterkinematic/PxController.h>
#include <physx/characterkinematic/PxCapsuleController.h>
#include <physx/characterkinematic/PxControllerManager.h>
#include <vector>

#include "PhysicsEventCallback.h"
#include "StaticRigidBody.h"
#include "DynamicRigidBody.h"
#include "CharacterController.h"
#include "RagdollPhysics.h"




class PhysicsEventCallback;
class ColliderInfo;
 
class PhysicX : public Singleton<PhysicX>
{
	friend class Singleton;
private:
	PhysicX() = default;
	~PhysicX() = default;
public:
	// IPhysicsSystem
	//core
	//물리엔진 세팅
	void Initialize();
	

	
	//물리엔진 업데이트
	void Update(float fixedDeltaTime);
	


	void AddActor(physx::PxActor* actor) { m_scene->addActor(*actor); }


	physx::PxPhysics* GetPhysics() { return m_physics; }
	physx::PxScene* GetPxScene() { return m_scene; }
	physx::PxMaterial* GetDefaultMaterial() { return m_defaultMaterial; }
	//physx::PxControllerManager* GetControllerManager() { return m_controllerManager; }
	//물리엔진 클리어 업데이트
	void FinalUpdate();

	//콜리전 이벤트 등록
	void SetCallBackCollisionFunction(std::function<void(CollisionData, ECollisionEventType)> func);

	//물리엔진 정보 수정
	void SetPhysicsInfo();

	//씬 체인지 시에 씬에 있는 모든 물리엔진 객체 삭제
	void ChangeScene();

	//충돌 검사를 위한 레이캐스트
	RayCastOutput RayCast(const RayCastInput& in, bool isStatic = false);

	//==========================================================================================
	//rigid body
	//생성
	void CreateStaticBody(const BoxColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateStaticBody(const SphereColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateStaticBody(const CapsuleColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateStaticBody(const ConvexMeshColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateStaticBody(const TriangleMeshColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateStaticBody(const HeightFieldColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix);
	void CreateDynamicBody(const BoxColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);
	void CreateDynamicBody(const SphereColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);
	void CreateDynamicBody(const CapsuleColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);
	void CreateDynamicBody(const ConvexMeshColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);
	void CreateDynamicBody(const TriangleMeshColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);
	void CreateDynamicBody(const HeightFieldColliderInfo& info, const EColliderType& colliderType, int* collisionMatrix, bool isKinematic);


	//StaticRigidBody* SettingStaticBody(physx::PxShape* shape,const ColliderInfo& colInfo,const  EColliderType& collideType,int* collisionMatrix,bool isGpuscene = false);
	StaticRigidBody* SettingStaticBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, int* collisionMatrix);
	//DynamicRigidBody* SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo,const EColliderType& collideType, int* collisionMatrix,bool isKinematic,bool isGpuscene = false);
	DynamicRigidBody* SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, int* collisionMatrix, bool isKinematic);

	//반환
	void GetRigidBodyData(unsigned int id,RigidBodyGetSetData& rigidBodyData);
	//수정
	void SetRigidBodyData(const unsigned int& id,const RigidBodyGetSetData& rigidBodyData,int* collisionMatrix);
	//삭제
	void RemoveRigidBody(const unsigned int& id,physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//전체 삭제
	void RemoveAllRigidBody(physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//==============================================
	//케릭터 컨트롤러
	//생성
	void CreateCCT(const CharacterControllerInfo& controllerInfo,const CharacterMovementInfo& movementInfo);
	//삭제
	void RemoveCCT(const unsigned int& id);
	//전체 삭제
	void RemoveAllCCT();
	//입력값 추가
	void AddInputMove(const CharactorControllerInputInfo& info);
	//getter setter
	CharacterControllerGetSetData GetCCTData(const unsigned int& id);
	CharacterMovementGetSetData GetMovementData(const unsigned int& id);
	void SetCCTData(const unsigned int& id ,const CharacterControllerGetSetData& controllerData);
	void SetMovementData(const unsigned int& id,const CharacterMovementGetSetData& movementData);
	//===================================================
	//케릭터 관절 정보 -> 레그돌
	//추가
	void CreateCharacterInfo(const ArticulationInfo& info);
	//삭제
	void RemoveCharacterInfo(const unsigned int& id);
	//모두삭제 
	void RemoveAllCharacterInfo();

	//관절에 링크 및 조인트 추가
	void AddArticulationLink(unsigned int id, LinkInfo& info, const DirectX::SimpleMath::Vector3& extent);
	void AddArticulationLink(unsigned int id, LinkInfo& info, const float& radius);
	void AddArticulationLink(unsigned int id, LinkInfo& info, const float& halfHeight,const float& radius);
	void AddArticulationLink(unsigned int id, LinkInfo& info);

	//getter setter
	ArticulationGetData GetArticulationData(const unsigned int& id);
	void SetArticulationData(const unsigned int& id, const ArticulationSetData& articulationData);

	//생성된 관절 겟수 반환
	unsigned int GetArticulationCount();
	//==========================================================
	//use Cuda PxDeformableSurface
	/*void CreateDeformableSurface();

	void GetDeformableSurfaceData();

	void SetDeformableSurfaceData();

	void RemoveDeformableSurface();

	void RemoveAllDeformableSurface();

	void GetDeformableSurfaceVertex();
	void GetDeformableSurfaceIndices();*/
	//todo:
	//==========================================================
	//컨벡스 메쉬 여부
	//void HasConvexMeshResource(const unsigned int& hash);
	//logger --> 아마 안쓸듯
	//void SetLogger();
	//삭제 예정 엑터 삭제
	void RemoveActors();

	void UnInitialize(); //물리엔진 종료
	void ShowNotRelease(); // 해제 되지 않은 객체들 출력
	void ConnectPVD(); //pvd 다시 연결

	//===========================================================================================
	//collision data ->  4quest CollisionDataManager 대응
	void CollisionDataUpdate();
	CollisionData* FindCollisionData(const unsigned int& id);
	void CreateCollisionData(const unsigned int& id, CollisionData* data);
	void RemoveCollisionData(const unsigned int& id);



private:
	physx::PxDefaultAllocator		m_allocator;
	physx::PxDefaultErrorCallback	m_errorCallback;

	
	physx::PxPvd* pvd = nullptr;
	physx::PxFoundation* m_foundation = nullptr;
	physx::PxPhysics* m_physics = nullptr;

	physx::PxScene* m_scene = nullptr;

	physx::PxDefaultCpuDispatcher* gDispatcher = nullptr;

	physx::PxMaterial* m_defaultMaterial = nullptr;

	//physx::PxControllerManager* m_controllerManager = nullptr;
	
	//===========================================================================================
	PhysicsEventCallback* m_eventCallback;

	int m_collisionMatrix[32];

	std::vector<physx::PxActor*> m_removeActorList; //삭제할 액터들

	//==================================================================================
	//rigid body 관리용
	std::unordered_map<unsigned int, RigidBody*> m_rigidBodyContainer; //rigid body 관리용
	std::vector<RigidBody*> m_updateActors; //업데이트 할 액터들

	//==================================================================================
	//collision data 관리용
	std::unordered_map<unsigned int, CollisionData*> m_collisionDataContainer; //collision data 관리용
	std::vector<unsigned int>m_removeCollisionIds; //삭제할 collision data id들

	//==================================================================================
	//character controller 관리용
	physx::PxControllerManager* m_characterControllerManager;
	std::unordered_map<unsigned int, CharacterController*> m_characterControllerContainer; //character controller 관리용

	std::vector<std::pair<CharacterControllerInfo, CharacterMovementInfo>> m_waittingCCTList; //대기중인 캐릭터 컨트롤러 리스트
	std::vector<std::pair<CharacterControllerInfo, CharacterMovementInfo>> m_updateCCTList; //업데이트 할 캐릭터 컨트롤러 리스트

	//==================================================================================
	//Ragdoll 관리용
	std::unordered_map<unsigned int, RagdollPhysics*> m_ragdollContainer; //ragdoll 관리용
	std::unordered_map<unsigned int, RagdollPhysics*> m_simulationRagdollContainer; //ragdoll 시뮬레이션 관리용



public:
	bool recordMemoryAllocations = true; // 디버그용 메모리 할당 추적

	
	physx::PxCudaContextManager* m_cudaContextManager = nullptr;
	physx::PxCudaContext* m_cudaContext = nullptr;


	/*PhysicsBufferPool bufferPool;
	void ReadPhysicsData();*/


	//====================================================================================================
	/*std::thread m_physicsThread;
	std::atomic<bool> m_isRunning{ true };
	std::mutex m_mutex;
	std::condition_variable m_cv;

	void PhysicsThreadFunc(float deltaTime);*/
	//====================================================================================================


};

inline static auto Physics = PhysicX::GetInstance();