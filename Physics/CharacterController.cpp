#include "CharacterController.h"

CharacterController::CharacterController()
{
}

CharacterController::~CharacterController()
{
	CollisionData* data = (CollisionData*)m_controller->getActor()->userData;
	data->isDead = true;

	if (m_controller)
	{
		m_controller->release();
		m_controller = nullptr;
	}
}

void CharacterController::Initialize(const CharacterControllerInfo& info, const CharacterMovementInfo& moveInfo, physx::PxControllerManager* CCTManager, physx::PxMaterial* material, CollisionData* collisionData, int* collisionMatrix)
{
	m_id = info.id;
	m_layerNumber = info.layerNumber;
	m_material = material;

	//케릭터 충돌 필터 설정
	m_filterData = new physx::PxFilterData();
	m_filterData->word0 = 0;
	m_filters = new physx::PxControllerFilters(m_filterData);

	m_characterMovement = new CharacterMovement();
	m_characterMovement->Initialize(moveInfo);
}

void CharacterController::Update(float deltaTime)
{
	//컨트롤러로 보여줄 이동 벡터
	physx::PxVec3 displayVector;

	//이동량 계산
	m_characterMovement->Update(deltaTime, m_inputMove, m_IsDynamic);
	m_characterMovement->OutputPxVector3(displayVector);

	//이동 제한
	if (displayVector.x<0.0f&&m_bMoveRestrict[static_cast<int>(ERestrictDirection::MINUS_X)])
	{
		displayVector.x = 0.0f;
	}
	else if (displayVector.x>0.0f&&m_bMoveRestrict[static_cast<int>(ERestrictDirection::PlUS_X)])
	{
		displayVector.x = 0.0f;
	}

	if (displayVector.z < 0.0f && m_bMoveRestrict[static_cast<int>(ERestrictDirection::MINUS_Z)])
	{
		displayVector.z = 0.0f;
	}
	else if (displayVector.z > 0.0f && m_bMoveRestrict[static_cast<int>(ERestrictDirection::PLUS_Z)])
	{
		displayVector.z = 0.0f;
	}

	//케릭터 컨트롤러 이동
	physx::PxControllerCollisionFlags collisionFlag = m_controller->move(displayVector, 0.01f, deltaTime, *m_filters);

	//바닥면 충돌 체크
	if (collisionFlag & physx::PxControllerCollisionFlag::eCOLLISION_DOWN) {
		m_characterMovement->SetIsFall(false);
	}
	else
	{
		m_characterMovement->SetIsFall(true);
	}


	//입력 초기화
	m_inputMove = {};
	m_IsDynamic = false;

}

void CharacterController::AddMovementInput(const DirectX::SimpleMath::Vector3& input, bool isDynamic)
{
	if (std::abs(input.x)>0)
	{
		m_inputMove.x = input.x;
	}
	if (std::abs(input.y) > 0)
	{
		m_inputMove.y = input.y;
	}
	if (std::abs(input.z) > 0)
	{
		m_inputMove.z = input.z;
	}

	m_IsDynamic = isDynamic;
}

bool CharacterController::ChangeLayerNumber(const unsigned int& newLayerNumber, int* collisionMatrix)
{
	if (newLayerNumber == UINT_MAX)
	{
		return false;
	}

	m_layerNumber = newLayerNumber;

	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];

	//
}
