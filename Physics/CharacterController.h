#pragma once
#include <physx/PxPhysicsAPI.h>
//#include "../Utility_Framework/Core.Minimal.h"
#include "PhysicsCommon.h"
#include "CharacterMovement.h"

class CharacterController
{
public:
	CharacterController();
	~CharacterController();

	void Initialize(const CharacterControllerInfo& info,const CharacterMovementInfo& moveInfo,physx::PxControllerManager* CCTManager,physx::PxMaterial* material,CollisionData* collisionData,int * collisionMatrix);
	void Update(float deltaTime);

	void AddMovementInput(const DirectX::SimpleMath::Vector3& input, bool isDynamic);

	bool ChangeLayerNumber(const unsigned int& newLayerNumber,int* collisionMatrix);

	void SetMoveRestrct(std::array<bool, 4> moveRestrict) {
		m_bMoveRestrict = moveRestrict;
	}

	inline physx::PxController* GetController() { return m_controller; }
	inline void SetController(physx::PxController* controller) { m_controller = controller; }
	inline const unsigned int& GetID() const { return m_id; }
	inline const unsigned int& GetLayerNumber() const { return m_layerNumber; }
	inline CharacterMovement* GetCharacterMovement() { return m_characterMovement; }
	inline void GetPosition(DirectX::SimpleMath::Vector3& position) const {
		position.x = m_controller->getPosition().x;
		position.y = m_controller->getPosition().y;
		position.z = m_controller->getPosition().z;
	}
	inline void SetPosition(const DirectX::SimpleMath::Vector3& position) {
		m_controller->setPosition(physx::PxExtendedVec3(position.x, position.y, position.z));
	}



protected:
	unsigned int m_id; //컨트롤러 ID
	unsigned int m_layerNumber; //충돌 레이어 번호

	DirectX::SimpleMath::Vector3 m_inputMove; 
	bool m_IsDynamic;
	std::array<bool, 4> m_bMoveRestrict;

	CharacterMovement* m_characterMovement; //캐릭터 이동 정보
	//todo : 케릭터 컨트롤러 충돌에 관한 필터 설정 -> 차후 추가
	//CharacterQueryFilterCallback* m_characterQueryFilterCallback; //쿼리 필터 콜백
	//CharacterControllerHitCallback* m_characterHitCallback; //히트 콜백

	physx::PxControllerFilters* m_filters;
	physx::PxFilterData* m_filterData;

	physx::PxMaterial* m_material;
	physx::PxController* m_controller;

};

