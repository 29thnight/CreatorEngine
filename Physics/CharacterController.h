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
	unsigned int m_id; //��Ʈ�ѷ� ID
	unsigned int m_layerNumber; //�浹 ���̾� ��ȣ

	DirectX::SimpleMath::Vector3 m_inputMove; 
	bool m_IsDynamic;
	std::array<bool, 4> m_bMoveRestrict;

	CharacterMovement* m_characterMovement; //ĳ���� �̵� ����
	//todo : �ɸ��� ��Ʈ�ѷ� �浹�� ���� ���� ���� -> ���� �߰�
	//CharacterQueryFilterCallback* m_characterQueryFilterCallback; //���� ���� �ݹ�
	//CharacterControllerHitCallback* m_characterHitCallback; //��Ʈ �ݹ�

	physx::PxControllerFilters* m_filters;
	physx::PxFilterData* m_filterData;

	physx::PxMaterial* m_material;
	physx::PxController* m_controller;

};

