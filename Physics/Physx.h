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
	//�������� ����
	void Initialize();
	

	
	//�������� ������Ʈ
	void Update(float fixedDeltaTime);
	


	void AddActor(physx::PxActor* actor) { m_scene->addActor(*actor); }


	physx::PxPhysics* GetPhysics() { return m_physics; }
	physx::PxScene* GetPxScene() { return m_scene; }
	physx::PxMaterial* GetDefaultMaterial() { return m_defaultMaterial; }
	//physx::PxControllerManager* GetControllerManager() { return m_controllerManager; }
	//�������� Ŭ���� ������Ʈ
	void FinalUpdate();

	//�ݸ��� �̺�Ʈ ���
	void SetCallBackCollisionFunction(std::function<void(CollisionData, ECollisionEventType)> func);

	//�������� ���� ����
	void SetPhysicsInfo();

	//�� ü���� �ÿ� ���� �ִ� ��� �������� ��ü ����
	void ChangeScene();

	//�浹 �˻縦 ���� ����ĳ��Ʈ
	RayCastOutput RayCast(const RayCastInput& in, bool isStatic = false);

	//==========================================================================================
	//rigid body
	//����
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

	//��ȯ
	void GetRigidBodyData(unsigned int id,RigidBodyGetSetData& rigidBodyData);
	//����
	void SetRigidBodyData(const unsigned int& id,const RigidBodyGetSetData& rigidBodyData,int* collisionMatrix);
	//����
	void RemoveRigidBody(const unsigned int& id,physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//��ü ����
	void RemoveAllRigidBody(physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//==============================================
	//�ɸ��� ��Ʈ�ѷ�
	//����
	void CreateCCT(const CharacterControllerInfo& controllerInfo,const CharacterMovementInfo& movementInfo);
	//����
	void RemoveCCT(const unsigned int& id);
	//��ü ����
	void RemoveAllCCT();
	//�Է°� �߰�
	void AddInputMove(const CharactorControllerInputInfo& info);
	//getter setter
	CharacterControllerGetSetData GetCCTData(const unsigned int& id);
	CharacterMovementGetSetData GetMovementData(const unsigned int& id);
	void SetCCTData(const unsigned int& id ,const CharacterControllerGetSetData& controllerData);
	void SetMovementData(const unsigned int& id,const CharacterMovementGetSetData& movementData);
	//===================================================
	//�ɸ��� ���� ���� -> ���׵�
	//�߰�
	void CreateCharacterInfo(const ArticulationInfo& info);
	//����
	void RemoveCharacterInfo(const unsigned int& id);
	//��λ��� 
	void RemoveAllCharacterInfo();

	//������ ��ũ �� ����Ʈ �߰�
	void AddArticulationLink(unsigned int id, LinkInfo& info, const DirectX::SimpleMath::Vector3& extent);
	void AddArticulationLink(unsigned int id, LinkInfo& info, const float& radius);
	void AddArticulationLink(unsigned int id, LinkInfo& info, const float& halfHeight,const float& radius);
	void AddArticulationLink(unsigned int id, LinkInfo& info);

	//getter setter
	ArticulationGetData GetArticulationData(const unsigned int& id);
	void SetArticulationData(const unsigned int& id, const ArticulationSetData& articulationData);

	//������ ���� �ټ� ��ȯ
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
	//������ �޽� ����
	//void HasConvexMeshResource(const unsigned int& hash);
	//logger --> �Ƹ� �Ⱦ���
	//void SetLogger();
	//���� ���� ���� ����
	void RemoveActors();

	void UnInitialize(); //�������� ����
	void ShowNotRelease(); // ���� ���� ���� ��ü�� ���
	void ConnectPVD(); //pvd �ٽ� ����

	//===========================================================================================
	//collision data ->  4quest CollisionDataManager ����
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

	std::vector<physx::PxActor*> m_removeActorList; //������ ���͵�

	//==================================================================================
	//rigid body ������
	std::unordered_map<unsigned int, RigidBody*> m_rigidBodyContainer; //rigid body ������
	std::vector<RigidBody*> m_updateActors; //������Ʈ �� ���͵�

	//==================================================================================
	//collision data ������
	std::unordered_map<unsigned int, CollisionData*> m_collisionDataContainer; //collision data ������
	std::vector<unsigned int>m_removeCollisionIds; //������ collision data id��

	//==================================================================================
	//character controller ������
	physx::PxControllerManager* m_characterControllerManager;
	std::unordered_map<unsigned int, CharacterController*> m_characterControllerContainer; //character controller ������

	std::vector<std::pair<CharacterControllerInfo, CharacterMovementInfo>> m_waittingCCTList; //������� ĳ���� ��Ʈ�ѷ� ����Ʈ
	std::vector<std::pair<CharacterControllerInfo, CharacterMovementInfo>> m_updateCCTList; //������Ʈ �� ĳ���� ��Ʈ�ѷ� ����Ʈ

	//==================================================================================
	//Ragdoll ������
	std::unordered_map<unsigned int, RagdollPhysics*> m_ragdollContainer; //ragdoll ������
	std::unordered_map<unsigned int, RagdollPhysics*> m_simulationRagdollContainer; //ragdoll �ùķ��̼� ������



public:
	bool recordMemoryAllocations = true; // ����׿� �޸� �Ҵ� ����

	
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