#pragma once
#include "Object.h"
#include "Transform.h"
#include "TypeTrait.h"
#include "Reflection.hpp"
#include "Component.generated.h"

class GameObject;
class Component : public Object
{
public:
   ReflectComponent
    [[Serializable(Inheritance:Object)]]
	GENERATED_BODY(Component)

	void SetOwner(GameObject* owner) { m_pOwner = owner; }
	GameObject* GetOwner() const { return m_pOwner; }

	void SetDestroy() { m_destroyMark = true; }
	//bool IsDestroyMark() const { return m_destroyMark; }

protected:
	GameObject*		m_pOwner{};
	Transform		m_transform{};
    [[Property]]
	HashedGuid		m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
	//bool			m_destroyMark{ false };
};
