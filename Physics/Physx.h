#pragma once
#include "DLLAcrossSingleton.h"
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
struct ColliderInfo;
 
class PhysicX : public DLLCore::Singleton<PhysicX>
{
	friend class DLLCore::Singleton<PhysicX>;
	using PolygonMesh = std::vector<std::vector<DirectX::SimpleMath::Vector3>>*;
private:
	PhysicX() = default;
	~PhysicX() = default;
public:
	// IPhysicsSystem
	//core
	//�������� ����
	bool Initialize();

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

	//���� ���� ����
	void DestroyActor(unsigned int id);


	//�浹 �˻縦 ���� ����ĳ��Ʈ
	RayCastOutput RayCast(const RayCastInput& in, bool isStatic = false);
	RayCastOutput Raycast(const RayCastInput& in);
	RayCastOutput RaycastAll(const RayCastInput& in);

	//==========================================================================================
	//rigid body
	//����
	void CreateStaticBody(const BoxColliderInfo& info, const EColliderType& colliderType);
	void CreateStaticBody(const SphereColliderInfo& info, const EColliderType& colliderType);
	void CreateStaticBody(const CapsuleColliderInfo& info, const EColliderType& colliderType);
	void CreateStaticBody(const ConvexMeshColliderInfo& info, const EColliderType& colliderType);
	void CreateStaticBody(const TriangleMeshColliderInfo& info, const EColliderType& colliderType);
	void CreateStaticBody(const HeightFieldColliderInfo& info, const EColliderType& colliderType);
	void CreateDynamicBody(const BoxColliderInfo& info, const EColliderType& colliderType, bool isKinematic);
	void CreateDynamicBody(const SphereColliderInfo& info, const EColliderType& colliderType, bool isKinematic);
	void CreateDynamicBody(const CapsuleColliderInfo& info, const EColliderType& colliderType, bool isKinematic);
	void CreateDynamicBody(const ConvexMeshColliderInfo& info, const EColliderType& colliderType, bool isKinematic);
	void CreateDynamicBody(const TriangleMeshColliderInfo& info, const EColliderType& colliderType, bool isKinematic);
	void CreateDynamicBody(const HeightFieldColliderInfo& info, const EColliderType& colliderType, bool isKinematic);


	//StaticRigidBody* SettingStaticBody(physx::PxShape* shape,const ColliderInfo& colInfo,const  EColliderType& collideType,int* collisionMatrix,bool isGpuscene = false);
	StaticRigidBody* SettingStaticBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, unsigned int* collisionMatrix);
	//DynamicRigidBody* SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo,const EColliderType& collideType, int* collisionMatrix,bool isKinematic,bool isGpuscene = false);
	DynamicRigidBody* SettingDynamicBody(physx::PxShape* shape, const ColliderInfo& colInfo, const EColliderType& collideType, unsigned int* collisionMatrix, bool isKinematic);

	//��ȯ
	RigidBodyGetSetData GetRigidBodyData(unsigned int id);
	RigidBody* GetRigidBody(const unsigned int& id);
	//����
	void SetRigidBodyData(const unsigned int& id,RigidBodyGetSetData& rigidBodyData); //&&&&& RigidBodyGetSetData const ������sehwan
	bool IsKinematic(unsigned int id) const;
	bool IsTrigger(unsigned int id) const;
	bool IsColliderEnabled(unsigned int id) const;
	bool IsUseGravity(unsigned int id) const;

	//���ο� ����
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

	void SetCollisionMatrix(unsigned int* collisionMatrix) {
		for (int i = 0; i < 32; ++i) {
			m_collisionMatrix[i] = collisionMatrix[i];
		}
	}
	unsigned int* GetCollisionMatrix() { return m_collisionMatrix; } //�浹 ��Ʈ���� ��ȯ
	void SetCollisionMatrix(unsigned int layer, unsigned int other, bool isCollision)
	{
		if (layer >= 32 || other >= 32) return; // out-of-range ����

		uint32_t bit = (1u << other);
		if (isCollision)
			m_collisionMatrix[layer] |= bit;   // �ѱ�
		else
			m_collisionMatrix[layer] &= ~bit;  // ����

		// ��Ī ���� ���� (layer��other)
		uint32_t bit2 = (1u << layer);
		if (isCollision)
			m_collisionMatrix[other] |= bit2;
		else
			m_collisionMatrix[other] &= ~bit2;
	}


	//===========================================================================================
	//debug data ����


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

	unsigned int m_collisionMatrix[32];

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

	//=================================================================================
	//Debug data ������
	std::unordered_map<unsigned int, PolygonMesh> mDebugPolygon;

	std::unordered_map<unsigned int, std::vector<unsigned int>> mDebugIndices;
	std::unordered_map<unsigned int, std::vector<DirectX::SimpleMath::Vector3>> mDebugVertices;

	std::unordered_map<unsigned int, std::vector<std::pair<DirectX::SimpleMath::Vector3, DirectX::SimpleMath::Vector3>>> mDebugHeightField;

	void extractDebugConvexMesh(physx::PxRigidActor* body, physx::PxShape* shape,std::vector<std::vector<DirectX::SimpleMath::Vector3>>& debuPolygon);



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

	void DrawPVDLine(DirectX::SimpleMath::Vector3 ori, DirectX::SimpleMath::Vector3 end);
};

static auto Physics = PhysicX::GetInstance();