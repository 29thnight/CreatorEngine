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

//raycast event ���� �Լ���� ���� ���κο� ���� �Ұ�
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
	// ��� �������� ���������� �����Ǵ� ����
	Entity* gameObject = nullptr;
	unsigned int layer = 0;

	// Raycast�� Sweep ���������� ��ȿ�� �����Դϴ�.
	// (Overlap�� ��� �⺻������ �����˴ϴ�.)
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
		Entity* gameObject;
		ICollider* collider;
		bool bIsDestroyed = false;
		bool bIsRemoveBody = false;
	};

	struct CollisionCallbackInfo {
		CollisionData data;
		ECollisionEventType Type;
	};
	// RigidBodyComponent�� PhysicsManager�� ���� ������ ��û�� �� ����ϴ� ����ü
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
	// �������� �ʱ�ȭ �� ������Ʈ
	void Initialize();
	void Update(float fixedDeltaTime);

	// �������� ����
	void Shutdown();

	// �������� �� ����
	void ChangeScene();

	//�� �ε� -> ���� ������ �� ������ü�� �����ϰ� ���� ���Ӿ��� ��ü�� ���� �������� ��ü�� ������ ���
	[[maybe_unused]] void OnLoadScene();

	//�� ��ε�
	void OnUnloadScene();

	//��ϵ� �ݹ��Լ� ����
	void ProcessCallback();

	//============================
	//raycast ���� �Լ���
	void RayCast(RayEvent& rayEvent);
	bool Raycast(RayEvent& rayEvent, RaycastHit& hit);
	int Raycast(RayEvent& rayEvent, std::vector<RaycastHit>& hits);
	//============================
	//Shape Sweep ���� �Լ���
	int BoxSweep(const SweepInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits);
	int SphereSweep(const SweepInput& in, float radius, std::vector<HitResult>& out_hits);
	int CapsuleSweep(const SweepInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);
	//============================
	//Shape Overlap ���� �Լ���
	int BoxOverlap(const OverlapInput& in, const math::vector3& boxExtent, std::vector<HitResult>& out_hits);
	int SphereOverlap(const OverlapInput& in, float radius, std::vector<HitResult>& out_hits);
	int CapsuleOverlap(const OverlapInput& in, float radius, float halfHeight, std::vector<HitResult>& out_hits);
	//============================
	
	//�浹 ��Ʈ���� ����
	void SetCollisionMatrix(std::vector<std::vector<uint8_t>> collisionGrid) {
		m_collisionMatrix = std::move(collisionGrid);
		unsigned int collisionMatrix[32] = { 0 };
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
	// 저작 게시는 Editor Host가 소유한다. 여기서는 YAML payload만 만들고 Player에는
	// handler가 없어 정상적으로 실패한다.
	bool SaveCollisionMatrix();
	void LoadCollisionMatrix();

	// RigidBody ���� ���� ��û (RigidBodyComponent���� ȣ��)
	void SetRigidBodyState(const RigidBodyState& state);

	// Rigidbody ���� ��ȸ
	bool IsRigidBodyKinematic(unsigned int id) const;
	bool IsRigidBodyTrigger(unsigned int id) const;
	bool IsRigidBodyColliderEnabled(unsigned int id) const;
	bool IsRigidBodyUseGravity(unsigned int id) const;


	// ������ CCT�� ���� �̵��� ���۽�ŵ�ϴ�.
	void ApplyForcedMoveToCCT(UINT controllerId, const math::vector3& initialVelocity);

	// ������ CCT�� ���� �̵��� ������ŵ�ϴ�.
	void StopForcedMoveOnCCT(UINT controllerId);

	// ������ CCT�� ���� ���� �̵� ������ Ȯ���մϴ�.
	bool IsInForcedMove(UINT controllerId) const;

	// CharacterController�� ��ġ�� ������ �����ϴ� �������̽� (���� ť�� �۾��� �߰��մϴ�)
	void SetControllerPosition(UINT id, const math::vector3& pos);

	//geometry �˻�
	//bool IsPenetrating();

private:
	using PendingChange = RigidBodyState;

	std::vector<PendingChange> m_pendingChanges;
	void ApplyPendingChanges();

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



	std::vector<std::vector<uint8_t>> m_collisionMatrix = std::vector<std::vector<uint8_t>>(32, std::vector<uint8_t>(32, true)); //�⺻ ���� ��� ���̾ �浹�ϴ� ������ ����
	
	// CCT ��ġ ������ ���� ��� ť
	struct PendingControllerPosition
	{
		UINT id;
		math::vector3 position;
	};
	std::vector<PendingControllerPosition> m_pendingControllerPositions;
	void ApplyPendingControllerPositionChanges(); // ����� CCT ��ġ ������ �����ϴ� �Լ�
	
	//�������� ��ü
	//std::unordered_map<ColliderID, ColliderInfo> m_colliderContainer;

	//�ݸ��� �ݹ� 
	std::vector<CollisionCallbackInfo> m_callbacks;
};

static auto PhysicsManagers = PhysicsManager::GetInstance();
