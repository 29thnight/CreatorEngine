#pragma once
#include "IComponent.h"
#include "Transform.h"
#include "TypeTrait.h"
#include "Reflection.hpp"

class GameObject;
class Component : public IComponent, public Meta::IReflectable<Component>
{
public:
	Component() = default;
	virtual ~Component() = default;

	std::string ToString() const override
	{
		return std::string("Component");
	}

	void SetOwner(GameObject* owner) { m_pOwner = owner; }
	GameObject* GetOwner() const { return m_pOwner; }

	void SetDestroy() { m_destroyMark = true; }
	bool IsDestroyMark() const { return m_destroyMark; }

	HashedGuid GetTypeID() const override final { return m_typeID; }
	HashedGuid GetInstanceID() const override final { return m_instanceID; }
	auto operator<=>(const Component& other) const { return m_typeID <=> other.m_typeID; }

	ReflectionField(Component)
	{
		PropertyField
		({
			meta_property(m_typeID)
			meta_property(m_instanceID)
		});

		FieldEnd(Component, PropertyOnly)
	}

protected:
	GameObject*		m_pOwner{};
	Transform		m_transform{};
	HashedGuid		m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
	HashedGuid		m_typeID{};
	bool			m_destroyMark{};
};