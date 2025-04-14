#pragma once
#include "../Utility_Framework/ClassProperty.h"
#include "../Utility_Framework/Core.Minimal.h"
#include <physx/PxPhysics.h>
#include <physx/PxPhysicsAPI.h>
#include <physx/characterkinematic/PxController.h>
#include <physx/characterkinematic/PxCapsuleController.h>
#include <physx/characterkinematic/PxControllerManager.h>
#include <vector>
#include "PhysicsInfo.h"
#include "PhysicsCommon.h"
#include "StaticRigidBody.h"
#include "DynamicRigidBody.h"


class PhysicsBufferPool {
public:
	std::vector<RigidbodyInfo*> buffers[2]; //
	std::atomic<int> activeBuffer{ 0 }; // 

	std::vector<RigidbodyInfo*>& GetWriteBuffer() {
		return buffers[activeBuffer.load() ^ 1]; // 
	}

	std::vector<RigidbodyInfo*>& GetReadBuffer() {
		return buffers[activeBuffer.load()]; // 
	}

	void SwapBuffers() {
		activeBuffer.store(activeBuffer.load() ^ 1); // 
	}
};


class PhysicX : public Singleton<PhysicX>
{
private:
	PhysicX() = default;
	~PhysicX() = default;
public:
	// IPhysicsSystem
	//core
	//�������� ����
	void Initialize();
	void UnInitialize();

	void PreUpdate();
	//�������� ������Ʈ
	void Update(float fixedDeltaTime);
	void PostUpdate();
	void PostUpdate(float interpolated);

	void AddRigidBody();

	void ClearActors();

	void AddActor(physx::PxActor* actor) { m_scene->addActor(*actor); }
	void ConnectPVD();

	physx::PxPhysics* GetPhysics() { return m_physics; }
	physx::PxScene* GetPxScene() { return m_scene; }
	physx::PxMaterial* GetDefaultMaterial() { return m_defaultMaterial; }
	physx::PxControllerManager* GetControllerManager() { return m_controllerManager; }
	//�������� Ŭ���� ������Ʈ
	void FinalUpdate();

	//�ݸ��� �̺�Ʈ ���
	void SetCallBackCollisionFunction(std::function<void(CollisionData, EColliderType)> func);

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
	void GetRigidBodyData(unsigned int id,const RigidBodyGetSetData& rigidBodyData);
	//����
	void SetRigidBodyData(const unsigned int& id,const RigidBodyGetSetData& rigidBodyData,int* collisionMatrix);
	//����
	void RemoveRigidBody(const unsigned int& id,physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//��ü ����
	void RemoveAllRigidBody(physx::PxScene* scene,std::vector<physx::PxActor*>& removeActorList);
	//==============================================
	//�ɸ��� ��Ʈ�ѷ�
	//����
	void CreateCCT();
	//����
	void RemoveCCT();
	//��ü ����
	void RemoveAllCCT();
	//�Է°� �߰�
	void AddInputMove();
	//getter setter
	void GetCCTData();
	void GetMovementData();
	void SetCCTData();
	void SetMovementData();
	//===================================================
	//�ɸ��� ���� ���� -> ���׵�
	//�߰�
	void CreateCharacterInfo();
	//����
	void RemoveCharacterInfo();
	//��λ��� 
	void RemoveAllCharacterInfo();

	//������ ��ũ �� ����Ʈ �߰�
	void AddArticulationLink();

	//getter setter
	void GetArticulationData();
	void SetArticulationData();

	//������ ���� �ټ� ��ȯ
	void GetArticulationCount();
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
	void HasConvexMeshResource();
	//logger --> �Ƹ� �Ⱦ���
	void SetLogger();
	//���� ���� ���� ����
	void RemoveActors();

	void UnInitialize(); //�������� ����
	void ShowNotRelease(); // ���� ���� ���� ��ü�� ���
	void ConnectPVD(); //pvd �ٽ� ����
private:
	physx::PxDefaultAllocator		m_allocator;
	physx::PxDefaultErrorCallback	m_errorCallback;

	physx::PxPvd* pvd = nullptr;
	physx::PxFoundation* m_foundation = nullptr;
	physx::PxPhysics* m_physics = nullptr;
	physx::PxPvd* pvd = nullptr;
	physx::PxScene* m_scene = nullptr;

	physx::PxDefaultCpuDispatcher* gDispatcher = nullptr;

	physx::PxMaterial* m_defaultMaterial = nullptr;

	physx::PxControllerManager* m_controllerManager = nullptr;

	//==================================================================================
	//rigid body ������
	std::unordered_map<unsigned int, RigidBody*> m_rigidBodyContainer; //rigid body ������
	std::vector<RigidBody*> m_updateActors; //������Ʈ �� ���͵�

	//==================================================================================
	//collision data ������
	std::unordered_map<unsigned int, CollisionData*> m_collisionDataContainer; //collision data ������
	std::vector<unsigned int>m_removeCollisionIds; //������ collision data id��

public:
	bool recordMemoryAllocations = true; // ����׿� �޸� �Ҵ� ����
	void ShowNotRelease();

	void GetShapes(std::vector<BoxShape>& out);
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