#pragma once
#include "Object.h"
#include "TypeTrait.h"
#include "Reflection.hpp"
#include "ManagedHeapObject.h"
#include "Component.generated.h"

class GameObject;
class Transform;
class Component : public Object, public ManagedHeapObject
{
public:
   ReflectComponent
    [[Serializable(Inheritance:Object)]]
	GENERATED_BODY(Component)

	void SetOwner(GameObject* owner);
	GameObject* GetOwner() const { return m_pOwner; }

	//TODO : �׽�Ʈ �ʿ�(������ inl�� ���)
	template<typename T>
	T& GetComponent();
	//Transform�� ��쿡�� GetComponent<Transform>()�� ���
	Component& GetComponent(HashedGuid typeof);

protected:
	GameObject*		m_pOwner{};
	Transform*		m_pTransform{ nullptr };
	[[Property]]
	FileGuid m_FileID{};
};

#include "Component.inl"
