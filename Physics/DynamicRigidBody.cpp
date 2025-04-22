#include "DynamicRigidBody.h"

DynamicRigidBody::DynamicRigidBody(EColliderType collidreType, unsigned int id, unsigned int layerNumber) : RigidBody(collidreType, id, layerNumber)
, m_rigidDynamic(nullptr)
{
}

DynamicRigidBody::~DynamicRigidBody()
{
	CollisionData* data = static_cast<CollisionData*>(m_rigidDynamic->userData);
	data->isDead = true;
	physx::PxShape* shape;
	m_rigidDynamic->getShapes(&shape, 1);
	m_rigidDynamic->detachShape(*shape);
}

bool DynamicRigidBody::Initialize(ColliderInfo colliderInfo, physx::PxShape* shape, physx::PxPhysics* physics, CollisionData* data, bool isKinematic)
{
	if (GetColliderType() == EColliderType::COLLISION)
	{
		shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
	}
	else {
		shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
		shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
	}

	data->thisId = m_id;
	data->thisLayerNumber = m_layerNumber;
	shape->userData = data;
	shape->setContactOffset(0.02f);
	shape->setRestOffset(0.01f);

	DirectX::SimpleMath::Matrix transform = colliderInfo.collsionTransform.worldMatrix;
	physx::PxTransform transformPx;
	CopyMatrixDxToPx(transform, transformPx);

	m_rigidDynamic = physics->createRigidDynamic(transformPx);
	m_rigidDynamic->userData = data;

	//kinematic 객체와 동적 객체의 차이
	//Kinematic 객체는 물리 엔진이 힘이나 충돌에 의해 자동으로 이동시키지 않습니다. 대신 사용자가 직접 위치나 회전을 업데이트합니다. 이러한 객체는 시네마틱 애니메이션이나 특정 제어가 필요한 경우에 유용
	//Dynamic 객체는 물리 엔진이 힘, 중력, 충돌 등에 의해 자동으로 이동시키는 객체입니다. 이 객체는 물리적 상호작용을 통해 자연스럽게 움직입니다. 예를 들어, 떨어지는 물체나 충돌하는 물체가 이에 해당합니다.

	//직접 제어 명시 및 충돌 연산 플래그 설정
	if (isKinematic) {
		m_rigidDynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
		m_rigidDynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, false);
	}
	else {
		m_rigidDynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, false);
		m_rigidDynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, true);
	}

	if (!m_rigidDynamic->attachShape(*shape)) {
		Debug->LogError("DynamicRigidBody::Initialize() : attachShape failed id :" + std::to_string(m_id));
		return false;
	}

	//rigidBody 확장 수집한 쉐이프의 부피에 따라 질량과 관성 모멘트를 업데이트
	physx::PxRigidBodyExt::updateMassAndInertia(*m_rigidDynamic, 1.0f);

	return true;
}

void DynamicRigidBody::ChangeLayerNumber(const unsigned int& layerNumber, int* collisionMatrix)
{
	if (layerNumber == UINT_MAX)
	{
		return;
	}
	
	m_layerNumber = layerNumber;

	physx::PxShape* shape;
	m_rigidDynamic->getShapes(&shape, 1);

	physx::PxFilterData newFilterData;

	newFilterData.word0 = layerNumber;
	newFilterData.word1 = collisionMatrix[layerNumber];

	shape->setSimulationFilterData(newFilterData);

	auto* data = static_cast<CollisionData*>(shape->userData);
	data->thisLayerNumber = m_layerNumber;
}

void DynamicRigidBody::SetConvertScale(const DirectX::SimpleMath::Vector3& scale, physx::PxPhysics* physics, int* collisionMatrix)
{
	//현제 스케일이 NaN인지 체크
	if (std::isnan(m_scale.x) || std::isnan(m_scale.y) || std::isnan(m_scale.z))
	{
		return;
	}

	//바꾸려는 스케일이 음수인지 체크--> - 크기는 존재할 수 없음
	if (scale.x < 0 || scale.y < 0 || scale.z < 0)
	{
		Debug->LogError("DynamicRigidBody::SetConvertScale() : scale is less than 0.001 id :" + std::to_string(m_id));
		return;
	}

	//스케일이 바뀌지 않았는지 체크 불필요 연산이므로
	if (fabs(m_scale.x - scale.x) < 0.001f && fabs(m_scale.y - scale.y) < 0.001f && fabs(m_scale.z - scale.z) < 0.001f)
	{
		return;
	}

	m_scale = scale;

	//크기를 바꾸기 위해 shape를 재작성 shape에 크기 이외의 다른 정보는 그대로 유지 하기위한 임시 포인터
	physx::PxShape* shape;
	physx::PxMaterial* material;
	m_rigidDynamic->getShapes(&shape, 1);
	shape->getMaterials(&material, 1);
	void* userData = shape->userData;

	//shape 데이터에 geometry를 가져와서 geometry의 타입에 따라 크기를 바꾸 주고 나머지 데이터는 그대로 유지
	if (shape->getGeometry().getType() == physx::PxGeometryType::eBOX)
	{
		physx::PxBoxGeometry boxGeometry = static_cast<const physx::PxBoxGeometry&>(shape->getGeometry());
		boxGeometry.halfExtents.x = m_Extent.x * scale.x;
		boxGeometry.halfExtents.y = m_Extent.y * scale.y;
		boxGeometry.halfExtents.z = m_Extent.z * scale.z;
		m_rigidDynamic->detachShape(*shape);

		UpdateShapeGeometry(m_rigidDynamic, boxGeometry, physics, material, collisionMatrix, userData);

	}
	else if (shape->getGeometry().getType() == physx::PxGeometryType::eSPHERE)
	{
		physx::PxSphereGeometry sphereGeometry = static_cast<const physx::PxSphereGeometry&>(shape->getGeometry());
		float maxRadius = std::max<float>(scale.x, std::max<float>(scale.y, scale.z));
		sphereGeometry.radius = m_radius * maxRadius;
		m_rigidDynamic->detachShape(*shape);
		UpdateShapeGeometry(m_rigidDynamic, sphereGeometry, physics, material, collisionMatrix, userData);
	}
	else if (shape->getGeometry().getType() == physx::PxGeometryType::eCAPSULE)
	{
		physx::PxCapsuleGeometry capsuleGeometry = static_cast<const physx::PxCapsuleGeometry&>(shape->getGeometry());
		float maxScale =  std::max<float>(scale.y, scale.z);
		capsuleGeometry.radius = m_radius * maxScale;

		capsuleGeometry.halfHeight = m_halfHeight * scale.x;
		m_rigidDynamic->detachShape(*shape);
		UpdateShapeGeometry(m_rigidDynamic, capsuleGeometry, physics, material, collisionMatrix, userData);
	}
	else if (shape->getGeometry().getType() == physx::PxGeometryType::eCONVEXMESH)
	{
		physx::PxConvexMeshGeometry convexMeshGeometry = static_cast<const physx::PxConvexMeshGeometry&>(shape->getGeometry());
		convexMeshGeometry.scale.scale.x = m_scale.x;
		convexMeshGeometry.scale.scale.y = m_scale.y;
		convexMeshGeometry.scale.scale.z = m_scale.z;
		m_rigidDynamic->detachShape(*shape);
		UpdateShapeGeometry(m_rigidDynamic, convexMeshGeometry, physics, material, collisionMatrix, userData);
	}
	else
	{
		Debug->LogError("DynamicRigidBody::SetConvertScale() : Unknown geometry type id :" + std::to_string(m_id));
	}
	
}
