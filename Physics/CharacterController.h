#pragma once
#include <physx/PxPhysicsAPI.h>
//#include "../Utility_Framework/Core.Minimal.h"
#include "PhysicsCommon.h"
#include "CharacterMovement.h"
//#include "Core.Mathf.h"
#include <functional>
#include <set>
using namespace physx;

enum class ECollisionEventType;
struct CollisionData;

class PhysicsControllerHitReport : public PxUserControllerHitReport
{
public:
	PhysicsControllerHitReport(std::function<void(const CollisionData&, ECollisionEventType)> callback);
	~PhysicsControllerHitReport();

	void onShapeHit(const PxControllerShapeHit& hit) override;
	void onControllerHit(const PxControllersHit& hit) override;
	void onObstacleHit(const PxControllerObstacleHit& hit) override;

	void UpdateAndDispatchEndEvents();

	void SetController(physx::PxController* controller) { m_controller = controller; }

private:
	physx::PxController* m_controller;
	std::function<void(const CollisionData&, ECollisionEventType)> m_callbackFunction;

	std::set<physx::PxActor*> m_currentContacts;
	std::set<physx::PxActor*> m_previousContacts;
};


class CharacterController
{
public:
	CharacterController();
	~CharacterController();

	void Initialize(const CharacterControllerInfo& info,const CharacterMovementInfo& moveInfo,physx::PxControllerManager* CCTManager,physx::PxMaterial* material,CollisionData* collisionData, unsigned int* collisionMatrix, std::function<void(CollisionData, ECollisionEventType)> callback);
	void Update(float deltaTime);

	void AddMovementInput(const DirectX::SimpleMath::Vector3& input, bool isDynamic);

	bool ChangeLayerNumber(const unsigned int& newLayerNumber, unsigned int* collisionMatrix);

	void SetMoveRestrct(std::array<bool, 4> moveRestrict) {
		m_bMoveRestrict = moveRestrict;
	}

	inline physx::PxController* GetController() { return m_controller; }
	inline void SetController(physx::PxController* controller) { m_controller = controller; }

	inline PhysicsControllerHitReport* GetHitReportCallback() { return m_hitReportCallback; }

	inline const unsigned int& GetID() const { return m_id; }
	inline const unsigned int& GetLayerNumber() const { return m_layerNumber; }
	inline CharacterMovement* GetCharacterMovement() { return m_characterMovement; }
	inline void GetPosition(DirectX::SimpleMath::Vector3& position) const {
		position.x = static_cast<float>(m_controller->getPosition().x);
		position.y = static_cast<float>(m_controller->getPosition().y);
		position.z = static_cast<float>(m_controller->getPosition().z);
	}
	inline void SetPosition(const DirectX::SimpleMath::Vector3& position) {
		m_controller->setPosition(physx::PxExtendedVec3(static_cast<double>(position.x), static_cast<double>(position.y), static_cast<double>(position.z)));
	}


	// 강제 이동(넉백, 대시) 상태를 시작시킵니다.
	//void StartForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration, int curveType);
	void StartForcedMove(const DirectX::SimpleMath::Vector3& initialVelocity, float duration);

	// 강제 이동을 즉시 중지시킵니다.
	void StopForcedMove();

	// 현재 강제 이동 상태인지 여부를 반환합니다.
	bool IsInForcedMove() const;

protected:
	unsigned int m_id; //컨트롤러 ID
	unsigned int m_layerNumber; //충돌 레이어 번호

	DirectX::SimpleMath::Vector3 m_inputMove; 
	bool m_IsDynamic;
	std::array<bool, 4> m_bMoveRestrict;

	CharacterMovement* m_characterMovement; //캐릭터 이동 정보
	
	PhysicsControllerHitReport* m_hitReportCallback;

	physx::PxControllerFilters* m_filters;
	physx::PxFilterData* m_filterData;

	physx::PxMaterial* m_material;
	physx::PxController* m_controller;

	// --- 강제 이동 상태 관리를 위한 새로운 멤버 변수들 ---
	bool m_isForcedMoveActive = false;
	float m_forcedMoveTimer = 0.f;
	float m_forcedMoveTotalDuration = 0.f;
	int m_currentCurveType = 0;
	float m_gravityWeight = 0.2f;
	DirectX::SimpleMath::Vector3 m_forcedMoveInitialVelocity;
	DirectX::SimpleMath::Vector3 m_forcedMoveCurrentVelocity;

};

class PhysicsControllerFilterCallback : public PxQueryFilterCallback
{
public:
	PhysicsControllerFilterCallback(unsigned int characterLayer, const unsigned int* matrix)
		: m_characterLayer(characterLayer), m_collisionMatrix(matrix) {}
	~PhysicsControllerFilterCallback() = default;

	virtual PxQueryHitType::Enum preFilter(
		const PxFilterData& filterData,
		const PxShape* shape,
		const PxRigidActor* actor,
		PxHitFlags& queryFlags) override
	{
		
		unsigned int otherLayer = shape->getSimulationFilterData().word0;

		

		if(m_collisionMatrix[m_characterLayer] & (1 << otherLayer)) 
		{

			// 충돌해야 한다면 BLOCK
			return PxQueryHitType::eBLOCK;
		}
		// 충돌하지 않아야 한다면 NONE
		return PxQueryHitType::eNONE;

	}
	virtual PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor)
	{
		return PxQueryHitType::eBLOCK;
	}

private:
	unsigned int m_characterLayer; // 캐릭터 레이어
	const unsigned int* m_collisionMatrix; //physics collision matrix ptr

public:
	void SetCharacterLayer(unsigned int layer) { m_characterLayer = layer; }
};

class CCTFilterCallback : public PxControllerFilterCallback
{
	// PxControllerFilterCallback을(를) 통해 상속됨
	bool filter(const PxController& a, const PxController& b) override {
		PxShape* shapeA = nullptr;
		a.getActor()->getShapes(&shapeA, 1);
		PxShape* shapeB = nullptr;
		b.getActor()->getShapes(&shapeB, 1);

		if (shapeA == nullptr || shapeB == nullptr) return false;

		auto shapeANum = shapeA->getQueryFilterData().word1;
		auto shapeBNum = shapeB->getQueryFilterData().word2;

		if ((shapeANum & shapeBNum) > 0) {
			return true;
		}

		return false;
	}
};