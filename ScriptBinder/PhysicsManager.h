#pragma once
#include "Core.Minimal.h"
#include "../physics/Physx.h"
#include "../physics/ICollider.h"
#include "GameObject.h"
#include "Component.h"



struct Collision 
{
	GameObject* thisObj;
	GameObject* otherObj;

	const std::vector<DirectX::SimpleMath::Vector3>& contactPoints;
};


//raycast event ���� �Լ���� ���� ���κο� ���� �Ұ�
struct RayEvent {
	struct ResultData {
		bool hasBlock;
		GameObject* blockObject;
		DirectX::SimpleMath::Vector3 blockPoint;

		unsigned int hitCount = -1;
		std::vector<GameObject*> hitObjects;
		std::vector<unsigned int> hitObjectLayer;
		std::vector<DirectX::SimpleMath::Vector3> hitPoints;
	};

	DirectX::SimpleMath::Vector3 origin = DirectX::SimpleMath::Vector3::Zero;
	DirectX::SimpleMath::Vector3 direction = DirectX::SimpleMath::Vector3::Zero;
	float distance = 0.0f;
	unsigned int layerMask = 0;

	ResultData* resultData = nullptr;
	bool isStatic = false;
	bool bUseDebugDraw = false;
};

struct ICollider;
class Component;
class GameObject;
class BoxColliderComponent;
class SphereColliderComponent;
class CapsuleColliderComponent;
class MeshColliderComponent;
class CharacterControllerComponent;
class TerrainColliderComponent;
class Scene;
class PhysicsManager : public Singleton<PhysicsManager>
{

	friend class Singleton;
	//todo : 
	// - �������� �ʱ�ȭ �� ������Ʈ
	// - �������� ����
	// - �������� �� ����
	// - Object�� ��ȸ�ϸ� ����������Ʈ�� ã�� ���� �� ������Ʈ �� ����
	// - �������� �ݸ��� �̺�Ʈ�� ã�Ƽ� �ݹ��Լ� ȣ��
	// - �������� ������Ʈ�� �����͸� ������� ����� ���� ��ο�
public:
	friend class Scene;
	using ColliderID = unsigned int;
	struct ColliderInfo
	{
		uint32_t id;
		Component* component;
		GameObject* gameObject;
		ICollider* collider;
		bool bIsDestroyed = false;
		bool bIsRemoveBody = false;
	};

	
	struct CollisionCallbackInfo {
		CollisionData data;
		ECollisionEventType Type;
	};

public:

	PhysicsManager() = default;
	~PhysicsManager() = default;

	// �������� �ʱ�ȭ �� ������Ʈ
	void Initialize();
	void Update(float fixedDeltaTime);

	// �������� ����
	void Shutdown();

	// �������� �� ����
	void ChangeScene();

	//�� �ε� -> ���� ������ �� ������ü�� �����ϰ� ���� ���Ӿ��� ��ü�� ���� �������� ��ü�� ������ ���
	void OnLoadScene();

	//�� ��ε�
	void OnUnloadScene();

	//��ϵ� �ݹ��Լ� ����
	void ProcessCallback();


	//����� ���� ��ο� //[[maybe_unused]] todo : DebugSystem ����
	[[maybe_unused]] 
	void DrawDebugInfo();


	void RayCast(RayEvent& rayEvent);
	
	//�浹 ��Ʈ���� ����
	void SetCollisionMatrix(std::vector<std::vector<uint8_t>> collisionGrid) {
		m_collisionMatrix = std::move(collisionGrid);
		int collisionMatrix[32] = { 0 };
		for (int i = 0; i < 32; ++i) {
			collisionMatrix[i] = 0; // �ʱ�ȭ
			for (int j = 0; j < 32; ++j) {
				if (m_collisionMatrix[i][j] != 0) {
					collisionMatrix[i] |= (1 << j);
				}
			}
		}
		Physics->SetCollisionMatrix(collisionMatrix);
	}
	std::vector<std::vector<uint8_t>> GetCollisionMatrix() const { return m_collisionMatrix; }

private:
	// �ʱ�ȭ ����
	bool m_bIsInitialized{ false };

	// �������� �ùķ���Ʈ ����
	bool m_bPlay{ false };

	//����� ��ο� ����
	bool m_bDebugDraw{ false };

	//���ε� �Ϸ� ����
	bool m_bIsLoaded{ false };

	//================
	//terrain
	//void AddTerrainCollider(GameObject* object);

	//
	//void AddCollider(GameObject* object);
	void AddCollider(BoxColliderComponent* box);
	void AddCollider(SphereColliderComponent* sphere);
	void AddCollider(CapsuleColliderComponent* capsule);
	void AddCollider(MeshColliderComponent* mesh);
	void AddCollider(CharacterControllerComponent* controller);
	void AddCollider(TerrainColliderComponent* terrain);

	//void RemoveCollider(GameObject* object);
	void RemoveCollider(BoxColliderComponent* box);
	void RemoveCollider(SphereColliderComponent* sphere);
	void RemoveCollider(CapsuleColliderComponent* capsule);
	void RemoveCollider(MeshColliderComponent* mesh);
	void RemoveCollider(CharacterControllerComponent* controller);
	void RemoveCollider(TerrainColliderComponent* terrain);

	void RemoveRagdollCollider(GameObject* object);
	void CallbackEvent(CollisionData data, ECollisionEventType type);
	//
	void CalculateOffset(DirectX::SimpleMath::Vector3 offset, GameObject* object);


	Core::DelegateHandle m_OnSceneLoadHandle;
	Core::DelegateHandle m_OnSceneUnloadHandle;
	Core::DelegateHandle m_OnChangeSceneHandle;

	//pre update  GameObject data -> pxScene data
	void SetPhysicData();

	//post update pxScene data -> GameObject data
	void GetPhysicData();

	unsigned int m_lastColliderID{ 0 };



	std::vector<std::vector<uint8_t>> m_collisionMatrix = std::vector<std::vector<uint8_t>>(32, std::vector<uint8_t>(32, true)); //�⺻ ���� ��� ���̾ �浹�ϴ� ������ ����
	


	
	//�������� ��ü
	//std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//�ݸ��� �ݹ� 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

static auto& PhysicsManagers = PhysicsManager::GetInstance();

