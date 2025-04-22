#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "BoxColliderComponent.generated.h"

class BoxColliderComponent : public Component
{
public:
   ReflectBoxColliderComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(BoxColliderComponent)
	void SetSize(const Mathf::Vector3& size) { m_sizeX = size.x; m_sizeY = size.y; m_sizeZ = size.z; }
	const Mathf::Vector3& GetSize() const { return Mathf::Vector3{m_sizeX,m_sizeY,m_sizeZ}; }
	void SetIsTrigger(bool isTrigger) { m_isTrigger = isTrigger; }
	bool IsTrigger() const { return m_isTrigger; }
	void SetOffset(const Mathf::Vector3& offset) { m_offsetX = offset.x; m_offsetY = offset.y; m_offsetZ = offset.z; }
	const Mathf::Vector3& GetOffset() const { return Mathf::Vector3{ m_offsetX,m_offsetY,m_offsetZ }; }

	[[Property]]
	float m_sizeX = 1.0f;
	[[Property]]
	float m_sizeY = 1.0f;
	[[Property]]
	float m_sizeZ = 1.0f;
	[[Property]]
	bool m_isTrigger = false;
	[[Property]]
	float m_offsetX = 0.0f;
	[[Property]]
	float m_offsetY = 0.0f;
	[[Property]]
	float m_offsetZ = 0.0f;
	

};

