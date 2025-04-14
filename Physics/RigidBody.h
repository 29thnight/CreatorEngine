#pragma once
#include <physx/PxPhysicsAPI.h>
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
	inline const Mathf::Vector3& GetExtent() const { return m_Extent; }
	inline const Mathf::Vector3& GetScale() const { return m_scale; }
	inline void SetRadius(const float& radius) { m_radius = radius; }
	inline void SetHalfHeight(const float& halfHeight) { m_halfHeight = halfHeight; }
	inline void SetExtent(const float& x, const float& y, const float& z) { m_Extent = { x,y,z }; }
	inline void SetScale(const Mathf::Vector3& scale) { m_scale = scale; }

	virtual void SetConvertScale(const Mathf::Vector3& scale, physx::PxPhysics* physics, int* collisionMatrix) abstract;

	inline void SetOffsetTranslation(const Mathf::Matrix offsetTranslation) {
		m_offsetTranslation = offsetTranslation;
	}
	inline const Mathf::Matrix& GetOffsetTranslation() const {
		return m_offsetTranslation;
	}
	inline void SetOffsetRotation(const Mathf::Matrix& offsetRotation) {
		m_offsetRotation = offsetRotation;
	}
	inline const Mathf::Matrix& GetOffsetRotation() const {
		return m_offsetRotation;
	}





protected:
	void UpdateShapeGeometry(physx::PxRigidActor* Actor, const physx::PxGeometry& newGeometry, physx::PxPhysics* physics, physx::PxMaterial* material, int* collisionMatrix, void* userData);

protected:
	unsigned int m_id; //자신 아이디
	unsigned int m_layerNumber;// 충돌 레이어 넘버
	EColliderType m_colliderType;//shape collider type

	Mathf::Matrix m_offsetRotation; // 모델 기준 회전
	Mathf::Matrix m_offsetTranslation; // 모델 기준 위치
	Mathf::Vector3 m_scale; // 배율
	float m_radius; // 원형,캡슐 경우 반지름
	float m_halfHeight; // 캡슐일경우 기둥 높이
	Mathf::Vector3 m_Extent; //박스일 경우 크기

};