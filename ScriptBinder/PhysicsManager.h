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


//raycast event 관련 함수등록 관련 메인부에 문의 할것
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

	// 물리엔진 초기화 및 업데이트
	void Initialize();
	void Update(float fixedDeltaTime);

	// 물리엔진 종료
	void Shutdown();

	// 물리엔진 씬 변경
	void ChangeScene();

	//씬 로드 -> 남아 있을지 모를 물리객체를 삭제하고 현제 게임씬의 객체에 대한 물리엔진 객체를 생성및 등록
	void OnLoadScene();

	//씬 언로드
	void OnUnloadScene();

	//등록된 콜백함수 실행
	void ProcessCallback();


	//디버그 정보 드로우 //[[maybe_unused]] todo : DebugSystem 통합
	[[maybe_unused]] 
	void DrawDebugInfo();


	void RayCast(RayEvent& rayEvent);
	
	//충돌 메트릭스 변경
	void SetCollisionMatrix(std::vector<std::vector<bool>> collisionGrid) {
		m_collisionMatrix = std::move(collisionGrid);
		int collisionMatrix[32] = { 0 };
		for (int i = 0; i < 32; ++i) {
			collisionMatrix[i] = 0; // 초기화
			for (int j = 0; j < 32; ++j) {
				collisionMatrix[i] |= (collisionGrid[i][j] ? (1 << j) : 0);
			}
		}
		Physics->SetCollisionMatrix(collisionMatrix);
	}
	std::vector<std::vector<bool>> GetCollisionMatrix() const { return m_collisionMatrix; }

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
	void AddTerrainCollider(GameObject* object);

	//
	void AddCollider(GameObject* object);
	void RemoveCollider(GameObject* object);
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



	std::vector<std::vector<bool>> m_collisionMatrix = std::vector<std::vector<bool>>(32, std::vector<bool>(32, true)); //기본 값은 모든 레이어가 충돌하는 것으로 설정
	


	
	//물리엔진 객체
	std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//콜리전 콜백 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

static auto& PhysicsManagers = PhysicsManager::GetInstance();

