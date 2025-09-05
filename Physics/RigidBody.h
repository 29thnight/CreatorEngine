#pragma once
#include <physx/PxPhysicsAPI.h>
#include <directxtk/SimpleMath.h>
#include "PhysicsCommon.h"

class RigidBody {
public:
	RigidBody(EColliderType collidreType, unsigned int id, unsigned int layerNumber);
	virtual ~RigidBody();

	inline const unsigned int GetID() const { return m_id; }
	inline const unsigned int GetLayerNumber() const { return m_layerNumber; }
	inline const EColliderType& GetColliderType() const { return m_colliderType; }
	inline const float& GetRadius() const { return m_radius; }
	inline const float& GetHalfHeight() const { return m_halfHeight; }
	inline const DirectX::SimpleMath::Vector3& GetExtent() const { return m_Extent; }
	inline const DirectX::SimpleMath::Vector3& GetScale() const { return m_scale; }
	inline void SetRadius(const float& radius) { m_radius = radius; }
	inline void SetHalfHeight(const float& halfHeight) { m_halfHeight = halfHeight; }
	inline void SetExtent(const float& x, const float& y, const float& z) { m_Extent = { x,y,z }; }
	inline void SetScale(const DirectX::SimpleMath::Vector3& scale) { m_scale = scale; }

	virtual void SetConvertScale(const DirectX::SimpleMath::Vector3& scale, physx::PxPhysics* physics, unsigned int* collisionMatrix) abstract;

	inline void SetOffsetPosition(const DirectX::SimpleMath::Vector3 offsetTranslation) {
		m_offsetPsition = offsetTranslation;
	}
	inline const DirectX::SimpleMath::Vector3& GetOffsetPosition() const {
		return m_offsetPsition;
	}
	inline void SetOffsetRotation(const DirectX::SimpleMath::Quaternion& offsetRotation) {
		m_offsetRotation = offsetRotation;
	}
	inline const DirectX::SimpleMath::Quaternion& GetOffsetRotation() const {
		return m_offsetRotation;
	}



protected:
	void UpdateShapeGeometry(physx::PxRigidActor* Actor, const physx::PxGeometry& newGeometry, physx::PxPhysics* physics, physx::PxMaterial* material, unsigned int* collisionMatrix, void* userData);

protected:
	unsigned int m_id; //�ڽ� ���̵�
	unsigned int m_layerNumber;// �浹 ���̾� �ѹ�
	EColliderType m_colliderType;//shape collider type

	
	//DirectX::SimpleMath::Matrix m_offsetRotation; // �� ���� ȸ��
	//DirectX::SimpleMath::Matrix m_offsetTranslation; // �� ���� ��ġ
	DirectX::SimpleMath::Vector3 m_scale; // ����
	DirectX::SimpleMath::Quaternion m_offsetRotation; // �� ���� ȸ��
	DirectX::SimpleMath::Vector3 m_offsetPsition; // �� ���� ��ġ
	float m_radius; // ����,ĸ�� ��� ������
	float m_halfHeight; // ĸ���ϰ�� ��� ����
	DirectX::SimpleMath::Vector3 m_Extent; //�ڽ��� ��� ũ��

};