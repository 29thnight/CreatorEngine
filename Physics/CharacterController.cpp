#include "CharacterController.h"
#include "Core.Memory.hpp"
#include "PhysicsHelper.h"
#include <iostream>

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
	//터지니? 만약에 중앙 통제면, 메니저에서 할당하고 넘겨줘야지...
	Memory::SafeDelete(m_characterMovement);
	Memory::SafeDelete(m_filters);
	Memory::SafeDelete(m_filterData);
}

void CharacterController::Initialize(const CharacterControllerInfo& info, const CharacterMovementInfo& moveInfo, physx::PxControllerManager* CCTManager, physx::PxMaterial* material, CollisionData* collisionData, unsigned int* collisionMatrix, std::function<void(CollisionData, ECollisionEventType)> callback)
{
	m_id = info.id;
	m_layerNumber = info.layerNumber;
	m_material = material;

	//케릭터 충돌 필터 설정
	m_filterData = new physx::PxFilterData();
	m_filterData->word0 = 0;		//layer number
	m_filterData->word2 = 1 << 0;	// layer bitmask
	m_filters = new physx::PxControllerFilters(m_filterData);
	m_filters->mFilterCallback = new PhysicsControllerFilterCallback(m_layerNumber, collisionMatrix); //&&&&&filter
	m_characterMovement = new CharacterMovement();
	m_characterMovement->Initialize(moveInfo);

	m_hitReportCallback = new PhysicsControllerHitReport(callback);
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

	if (m_hitReportCallback)
	{
		m_hitReportCallback->UpdateAndDispatchEndEvents();
	}

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

bool CharacterController::ChangeLayerNumber(const unsigned int& newLayerNumber, unsigned int* collisionMatrix)
{
	if (newLayerNumber == UINT_MAX)
	{
		return false;
	}

	m_layerNumber = newLayerNumber;

	physx::PxFilterData filterData;
	filterData.word0 = m_layerNumber;
	filterData.word1 = collisionMatrix[m_layerNumber];
	filterData.word2 = 1 << m_layerNumber;

	m_filterData->word0 = m_layerNumber;
	m_filterData->word1 = collisionMatrix[m_layerNumber];
	m_filterData->word2 = 1 << m_layerNumber;

	//
}


PhysicsControllerHitReport::PhysicsControllerHitReport(std::function<void(const CollisionData&, ECollisionEventType)> callback)
	: m_controller(nullptr), m_callbackFunction(callback)
{
}

PhysicsControllerHitReport::~PhysicsControllerHitReport()
{
}

void PhysicsControllerHitReport::onShapeHit(const PxControllerShapeHit& hit)
{
	//collider 
	CollisionData* controllerData = (CollisionData*)hit.controller->getActor()->userData;
	CollisionData* shapeData = (CollisionData*)hit.actor->userData;

	if (controllerData == nullptr || shapeData == nullptr || m_callbackFunction == nullptr) return;

	m_currentContacts.insert(hit.actor);

	ECollisionEventType eventType;
	if (m_previousContacts.find(hit.actor) == m_previousContacts.end())
	{
		eventType = ECollisionEventType::ENTER_COLLISION;
	}
	else
	{
		eventType = ECollisionEventType::ON_COLLISION;
	}

	std::vector<DirectX::SimpleMath::Vector3> contactPoints;
	DirectX::SimpleMath::Vector3 contactPoint;
	physx::PxExtendedVec3 extendedPos = hit.worldPos;
	physx::PxVec3 floatPos(
		static_cast<float>(extendedPos.x),
		static_cast<float>(extendedPos.y),
		static_cast<float>(extendedPos.z)
	);
	ConvertVectorPxToDx(floatPos, contactPoint); // PhysX 벡터를 엔진 벡터로 변환
	contactPoints.push_back(contactPoint);          // 벡터에 추가

	CollisionData firstActor;
	firstActor.thisId = controllerData->thisId;
	firstActor.otherId = shapeData->thisId;
	firstActor.thisLayerNumber = controllerData->thisLayerNumber;
	firstActor.otherLayerNumber = shapeData->thisLayerNumber;
	firstActor.contactPoints = contactPoints;

	CollisionData secondActor;
	secondActor.thisId = shapeData->thisId;
	secondActor.otherId = controllerData->thisId;
	secondActor.thisLayerNumber = shapeData->thisLayerNumber;
	secondActor.otherLayerNumber = controllerData->thisLayerNumber;
	secondActor.contactPoints = contactPoints;

	//std::cout << "PhysicsControllerHitReport::onShapeHit - thisId: " << firstActor.thisId << ", otherId: " << firstActor.otherId << ", EventType: " << static_cast<int>(eventType) << std::endl;

	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);
}

void PhysicsControllerHitReport::onControllerHit(const PxControllersHit& hit)
{
	// CCT와 CCT의 충돌을 처리합니다.
	CollisionData* controllerData = (CollisionData*)hit.controller->getActor()->userData;
	CollisionData* otherControllerData = (CollisionData*)hit.other->getActor()->userData;

	if (controllerData == nullptr || otherControllerData == nullptr || m_callbackFunction == nullptr) return;

	// 현재 충돌한 액터(상대방 CCT)를 목록에 추가합니다.
	m_currentContacts.insert(hit.other->getActor());

	ECollisionEventType eventType;
	// 이전 프레임에 충돌 목록에 없었다면 ENTER
	if (m_previousContacts.find(hit.other->getActor()) == m_previousContacts.end())
	{
		eventType = ECollisionEventType::ENTER_COLLISION;
	}
	else // 있었다면 STAY
	{
		eventType = ECollisionEventType::ON_COLLISION;
	}

	// [수정] CCT 간 충돌에서도 충돌 지점 정보를 추가합니다.
	std::vector<DirectX::SimpleMath::Vector3> contactPoints;
	DirectX::SimpleMath::Vector3 contactPoint;
	physx::PxExtendedVec3 extendedPos = hit.worldPos;
	physx::PxVec3 floatPos(
		static_cast<float>(extendedPos.x),
		static_cast<float>(extendedPos.y),
		static_cast<float>(extendedPos.z)
	);
	ConvertVectorPxToDx(floatPos, contactPoint);
	contactPoints.push_back(contactPoint);

	// CollisionData를 생성하고 정보를 채웁니다.
	CollisionData firstActor;
	firstActor.thisId = controllerData->thisId;
	firstActor.otherId = otherControllerData->thisId;
	firstActor.thisLayerNumber = controllerData->thisLayerNumber;
	firstActor.otherLayerNumber = otherControllerData->thisLayerNumber;
	firstActor.contactPoints = contactPoints; // 할당

	CollisionData secondActor;
	secondActor.thisId = otherControllerData->thisId;
	secondActor.otherId = controllerData->thisId;
	secondActor.thisLayerNumber = otherControllerData->thisLayerNumber;
	secondActor.otherLayerNumber = controllerData->thisLayerNumber;
	secondActor.contactPoints = contactPoints; // 할당

	m_callbackFunction(firstActor, eventType);
	m_callbackFunction(secondActor, eventType);
}

void PhysicsControllerHitReport::onObstacleHit(const PxControllerObstacleHit& hit)
{
}

void PhysicsControllerHitReport::UpdateAndDispatchEndEvents()
{
	if (!m_controller) return;

	for (auto* actor : m_previousContacts)
	{		
		// END_COLLISION ´ ´ ´
		// ¹
		if (m_currentContacts.find(actor) == m_currentContacts.end())
		{
			CollisionData* controllerData = (CollisionData*)m_controller->getActor()->userData;
			CollisionData* shapeData = (CollisionData*)actor->userData;

			if (controllerData == nullptr || shapeData == nullptr || m_callbackFunction == nullptr) continue;

			CollisionData firstActor, secondActor;
			firstActor.thisId = controllerData->thisId;
			firstActor.otherId = shapeData->thisId;
			secondActor.thisId = shapeData->thisId;
			secondActor.otherId = controllerData->thisId;

			m_callbackFunction(firstActor, ECollisionEventType::END_COLLISION);
			m_callbackFunction(secondActor, ECollisionEventType::END_COLLISION);
		}
	}

	m_previousContacts = m_currentContacts;

	//std::cout << "PhysicsControllerHitReport::UpdateAndDispatchEndEvents - Current Contacts Size: " << m_currentContacts.size() << std::endl;

	m_currentContacts.clear();
}
