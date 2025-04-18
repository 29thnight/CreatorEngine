#pragma once
#include "Object.h"
#include "Transform.h"
#include "TypeTrait.h"
#include "Reflection.hpp"

class GameObject;
class Component : public Object, public Meta::IReflectable<Component>
{
public:
	GENERATED_BODY(Component)

	void SetOwner(GameObject* owner) { m_pOwner = owner; }
	GameObject* GetOwner() const { return m_pOwner; }

	void SetDestroy() { m_destroyMark = true; }
	bool IsDestroyMark() const { return m_destroyMark; }

	ReflectionField(Component)
	{
		PropertyField
		({
			meta_property(m_instanceID)
		});

		FieldEnd(Component, PropertyOnly)
	}

protected:
	GameObject*		m_pOwner{};
	Transform		m_transform{};
	HashedGuid		m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
	bool			m_destroyMark{ false };
};