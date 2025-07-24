#pragma once
#include "Core.Minimal.h"
#include "../Physics/Physx.h"
#include "../Physics/ICollider.h"

class Component;
class GameObject;
struct Collision
{
	GameObject* thisObj;
	GameObject* otherObj;

	const std::vector<Mathf::Vector3>& contactPoints;
};

//raycast event 관련 함수등록 관련 메인부에 문의 할것
struct RayEvent {
	struct ResultData {
		bool hasBlock;
		GameObject* blockObject;
		DirectX::SimpleMath::Vector3 blockPoint;
		DirectX::SimpleMath::Vector3 blockNormal;

		unsigned int hitCount = -1;
		std::vector<GameObject*> hitObjects;
		std::vector<unsigned int> hitObjectLayer;
		std::vector<DirectX::SimpleMath::Vector3> hitPoints;
		std::vector<DirectX::SimpleMath::Vector3> hitNormals;
	};

	DirectX::SimpleMath::Vector3 origin = DirectX::SimpleMath::Vector3::Zero;
	DirectX::SimpleMath::Vector3 direction = DirectX::SimpleMath::Vector3::Zero;
	float distance = 0.0f;
	unsigned int layerMask = 0;

	ResultData* resultData = nullptr;
	bool isStatic = false;
	bool bUseDebugDraw = false;
};

struct RaycastHit {
	GameObject* hitObject = nullptr;
	DirectX::SimpleMath::Vector3 hitPoint{};
	DirectX::SimpleMath::Vector3 hitNormal{};
	unsigned int hitObjectLayer = 0;
};

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
	// - 물리엔진 초기화 및 업데이트
	// - 물리엔진 종료
	// - 물리엔진 씬 변경
	// - Object를 순회하며 물리컴포넌트를 찾아 생성 및 업데이트 및 삭제
	// - 물리엔진 콜리전 이벤트를 찾아서 콜백함수 호출
	// - 물리엔지 컴포넌트의 데이터를 기반으로 디버그 정보 드로우
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
	// RigidBodyComponent가 PhysicsManager로 상태 변경을 요청할 때 사용하는 구조체
	struct RigidBodyState
	{
		unsigned int id;
		bool isKinematic;
		bool isTrigger;
		bool isColliderEnabled;
		bool useGravity;
	};
public:

	PhysicsManager() = default;
	~PhysicsManager() = default;

	// 물리엔진 초기화 및 업데이트
	void Initialize();
	void Update(float fixedDeltaTime);

	// 물리엔진 종료
	void Shutdown();

	// 물리엔진 씬 변경
	void ChangeScene();

	//씬 로드 -> 남아 있을지 모를 물리객체를 삭제하고 현제 게임씬의 객체에 대한 물리엔진 객체를 생성및 등록
	[[maybe_unused]] void OnLoadScene();

	//씬 언로드
	void OnUnloadScene();

	//등록된 콜백함수 실행
	void ProcessCallback();


	//디버그 정보 드로우 //[[maybe_unused]] todo : DebugSystem 통합
	[[maybe_unused]] 
	void DrawDebugInfo();


	void RayCast(RayEvent& rayEvent);
	bool Raycast(RayEvent& rayEvent, RaycastHit& hit);
	int Raycast(RayEvent& rayEvent, std::vector<RaycastHit>& hits);
	
	//충돌 메트릭스 변경
	void SetCollisionMatrix(std::vector<std::vector<uint8_t>> collisionGrid) {
		m_collisionMatrix = std::move(collisionGrid);
		unsigned int collisionMatrix[32] = { 0 };
		for (int i = 0; i < 32; ++i) {
			collisionMatrix[i] = 0; // 초기화
			for (int j = 0; j < 32; ++j) {
				if (m_collisionMatrix[i][j] != 0) {
					collisionMatrix[i] |= (1 << j);
				}
			}
		}
		Physics->SetCollisionMatrix(collisionMatrix);
	}
	std::vector<std::vector<uint8_t>> GetCollisionMatrix() const { return m_collisionMatrix; }

	// RigidBody 상태 변경 요청 (RigidBodyComponent에서 호출)
	void SetRigidBodyState(const RigidBodyState& state);

	// Rigidbody 상태 조회
	bool IsRigidBodyKinematic(unsigned int id) const;
	bool IsRigidBodyTrigger(unsigned int id) const;
	bool IsRigidBodyColliderEnabled(unsigned int id) const;
	bool IsRigidBodyUseGravity(unsigned int id) const;

private:
	using PendingChange = RigidBodyState;

	std::vector<PendingChange> m_pendingChanges;
	void ApplyPendingChanges();

private:
	// 초기화 여부
	bool m_bIsInitialized{ false };

	// 물리엔진 시뮬레이트 여부
	bool m_bPlay{ false };

	//디버그 드로우 여부
	bool m_bDebugDraw{ false };

	//씬로드 완료 여부
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



	std::vector<std::vector<uint8_t>> m_collisionMatrix = std::vector<std::vector<uint8_t>>(32, std::vector<uint8_t>(32, true)); //기본 값은 모든 레이어가 충돌하는 것으로 설정
	


	
	//물리엔진 객체
	//std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//콜리전 콜백 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

static auto& PhysicsManagers = PhysicsManager::GetInstance();
