#pragma once
#include "Core.Minimal.h"
#include "ClassProperty.h"
#include "../Physics/Physx.h"
#include "../Physics/ICollider.h"

class Component;
class Entity;
struct Collision
{
	Entity* thisObj;
	Entity* otherObj;

	const std::vector<math::vector3>& contactPoints;
};

//raycast event 관련 함수등록 관련 메인부에 문의 할것
struct RayEvent {
	struct ResultData {
		bool hasBlock;
		Entity* blockObject;
		math::vector3 blockPoint;
		math::vector3 blockNormal;

		unsigned int hitCount = -1;
		std::vector<Entity*> hitObjects;
		std::vector<unsigned int> hitObjectLayer;
		std::vector<math::vector3> hitPoints;
		std::vector<math::vector3> hitNormals;
	};

	math::vector3 origin{};
	math::vector3 direction{};
	float distance = 0.0f;
	unsigned int layerMask = 0;

	ResultData* resultData = nullptr;
	bool isStatic = false;
	bool bUseDebugDraw = false;
};

struct RaycastHit {
	Entity* hitObject = nullptr;
	math::vector3 hitPoint{};
	math::vector3 hitNormal{};
	unsigned int hitObjectLayer = 0;
};

struct HitResult {
	// 모든 쿼리에서 공통적으로 제공되는 정보
	Entity* gameObject = nullptr;
	unsigned int layer = 0;

	// Raycast와 Sweep 쿼리에서만 유효한 정보입니다.
	// (Overlap의 경우 기본값으로 유지됩니다.)
	math::vector3 point{};
	math::vector3 normal{};
	float distance = -1.0f;
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
private:
	friend class Singleton<PhysicsManager>;

	PhysicsManager() = default;
	~PhysicsManager() = default;
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
		Entity* gameObject;
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
		bool movePositionDirty = false;
		math::vector3 movePosition{};
	};
public:
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

	//============================
	//raycast 관련 함수들
	void RayCast(RayEvent& rayEvent);
	bool Raycast(RayEvent& rayEvent, RaycastHit& hit);
	int Raycast(RayEvent& rayEvent, std::vector<RaycastHit>& hits);
	//============================
	//Shape Sweep 관련 함수들
	int BoxSweep(const SweepInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits);
	int SphereSweep(const SweepInput& in, float radius, std::vector<HitResult>& out_hits);
	int CapsuleSweep(const SweepInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);
	//============================
	//Shape Overlap 관련 함수들
	int BoxOverlap(const OverlapInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits);
	int SphereOverlap(const OverlapInput& in, float radius, std::vector<HitResult>& out_hits);
	int CapsuleOverlap(const OverlapInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);
	//============================
	
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
	// 저작 게시는 Editor Host가 소유한다. 여기서는 YAML payload만 만들고 Player에는
	// handler가 없어 정상적으로 실패한다.
	bool SaveCollisionMatrix();
	void LoadCollisionMatrix();

	// RigidBody 상태 변경 요청 (RigidBodyComponent에서 호출)
	void SetRigidBodyState(const RigidBodyState& state);

	// Rigidbody 상태 조회
	bool IsRigidBodyKinematic(unsigned int id) const;
	bool IsRigidBodyTrigger(unsigned int id) const;
	bool IsRigidBodyColliderEnabled(unsigned int id) const;
	bool IsRigidBodyUseGravity(unsigned int id) const;


	// 지정된 CCT에 강제 이동을 시작시킵니다.
	void ApplyForcedMoveToCCT(UINT controllerId, const math::vector3& initialVelocity);

	// 지정된 CCT의 강제 이동을 중지시킵니다.
	void StopForcedMoveOnCCT(UINT controllerId);

	// 지정된 CCT가 현재 강제 이동 중인지 확인합니다.
	bool IsInForcedMove(UINT controllerId) const;

	// CharacterController의 위치를 강제로 설정하는 인터페이스 (이제 큐에 작업을 추가합니다)
	void SetControllerPosition(UINT id, const math::vector3& pos);

	//geometry 검사
	//bool IsPenetrating();

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
	//void AddTerrainCollider(Entity* object);

	//
	//void AddCollider(Entity* object);
	void AddCollider(BoxColliderComponent* box);
	void AddCollider(SphereColliderComponent* sphere);
	void AddCollider(CapsuleColliderComponent* capsule);
	void AddCollider(MeshColliderComponent* mesh);
	void AddCollider(CharacterControllerComponent* controller);
	void AddCollider(TerrainColliderComponent* terrain);

	//void RemoveCollider(Entity* object);
	void RemoveCollider(BoxColliderComponent* box);
	void RemoveCollider(SphereColliderComponent* sphere);
	void RemoveCollider(CapsuleColliderComponent* capsule);
	void RemoveCollider(MeshColliderComponent* mesh);
	void RemoveCollider(CharacterControllerComponent* controller);
	void RemoveCollider(TerrainColliderComponent* terrain);

	void RemoveRagdollCollider(Entity* object);
	void CallbackEvent(CollisionData data, ECollisionEventType type);
	Core::DelegateHandle m_OnSceneLoadHandle;
	Core::DelegateHandle m_OnSceneUnloadHandle;
	Core::DelegateHandle m_OnChangeSceneHandle;

	//pre update  Entity data -> pxScene data
	void SetPhysicData();

	//post update pxScene data -> Entity data
	void GetPhysicData();

	unsigned int m_lastColliderID{ 0 };



	std::vector<std::vector<uint8_t>> m_collisionMatrix = std::vector<std::vector<uint8_t>>(32, std::vector<uint8_t>(32, true)); //기본 값은 모든 레이어가 충돌하는 것으로 설정
	
	// CCT 위치 변경을 위한 펜딩 큐
	struct PendingControllerPosition
	{
		UINT id;
		math::vector3 position;
	};
	std::vector<PendingControllerPosition> m_pendingControllerPositions;
	void ApplyPendingControllerPositionChanges(); // 펜딩된 CCT 위치 변경을 적용하는 함수
	
	//물리엔진 객체
	//std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//콜리전 콜백 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

static auto PhysicsManagers = PhysicsManager::GetInstance();
