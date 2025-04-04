#pragma once
#include "IComponent.h"
#include "Transform.h"
#include "TypeTrait.h"

enum class ComponentOrder
{
	Transform = 0,
	Light,
	Camera,
	Physics,
	Animator,
	BehaviorScript,
	MeshRenderer,
	ComponentMax
};


class GameObject;
class Component : public IComponent
{
public:
	Component() = default;
	virtual ~Component() = default;

	void SetOwner(GameObject* owner) { m_pOwner = owner; }
	GameObject* GetOwner() const { return m_pOwner; }

	void SetDestroy() { m_destroyMark = true; }
	bool IsDestroyMark() const { return m_destroyMark; }

	uint32_t GetOrderID() const { return m_orderID; }
	uint32_t GetTypeID() const override final { return m_typeID; }
	size_t GetInstanceID() const override final { return m_instanceID; }
	auto operator<=>(const Component& other) const { return m_typeID <=> other.m_typeID; }

	static inline uint32_t Order2Uint(ComponentOrder order)
	{
		return static_cast<uint32_t>(order);
	}

protected:
	GameObject* m_pOwner{};
	Transform m_transform{};
	size_t m_instanceID{ TypeTrait::GUIDCreator::GetGUID() };
	uint32_t m_typeID{};
	uint32_t m_orderID{};
	bool m_destroyMark{};
};