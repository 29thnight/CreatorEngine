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

	//kinematic ��ü�� ���� ��ü�� ����
	//Kinematic ��ü�� ���� ������ ���̳� �浹�� ���� �ڵ����� �̵���Ű�� �ʽ��ϴ�. ��� ����ڰ� ���� ��ġ�� ȸ���� ������Ʈ�մϴ�. �̷��� ��ü�� �ó׸�ƽ �ִϸ��̼��̳� Ư�� ��� �ʿ��� ��쿡 ����
	//Dynamic ��ü�� ���� ������ ��, �߷�, �浹 � ���� �ڵ����� �̵���Ű�� ��ü�Դϴ�. �� ��ü�� ������ ��ȣ�ۿ��� ���� �ڿ������� �����Դϴ�. ���� ���, �������� ��ü�� �浹�ϴ� ��ü�� �̿� �ش��մϴ�.

	//���� ���� ��� �� �浹 ���� �÷��� ����
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

	//rigidBody Ȯ�� ������ �������� ���ǿ� ���� ������ ���� ���Ʈ�� ������Ʈ
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
	//���� �������� NaN���� üũ
	if (std::isnan(m_scale.x) || std::isnan(m_scale.y) || std::isnan(m_scale.z))
	{
		return;
	}

	//�ٲٷ��� �������� �������� üũ--> - ũ��� ������ �� ����
	if (scale.x < 0 || scale.y < 0 || scale.z < 0)
	{
		Debug->LogError("DynamicRigidBody::SetConvertScale() : scale is less than 0.001 id :" + std::to_string(m_id));
		return;
	}

	//�������� �ٲ��� �ʾҴ��� üũ ���ʿ� �����̹Ƿ�
	if (fabs(m_scale.x - scale.x) < 0.001f && fabs(m_scale.y - scale.y) < 0.001f && fabs(m_scale.z - scale.z) < 0.001f)
	{
		return;
	}

	m_scale = scale;

	//ũ�⸦ �ٲٱ� ���� shape�� ���ۼ� shape�� ũ�� �̿��� �ٸ� ������ �״�� ���� �ϱ����� �ӽ� ������
	physx::PxShape* shape;
	physx::PxMaterial* material;
	m_rigidDynamic->getShapes(&shape, 1);
	shape->getMaterials(&material, 1);
	void* userData = shape->userData;

	//shape �����Ϳ� geometry�� �����ͼ� geometry�� Ÿ�Կ� ���� ũ�⸦ �ٲ� �ְ� ������ �����ʹ� �״�� ����
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
