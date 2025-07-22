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

	//TODO : 테스트 필요(사유는 inl에 요약)
	template<typename T>
	T& GetComponent();
	//Transform의 경우에는 GetComponent<Transform>()로 사용
	Component& GetComponent(HashedGuid typeof);

protected:
	GameObject*		m_pOwner{};
	Transform*		m_pTransform{ nullptr };
	[[Property]]
	FileGuid m_FileID{};
};

#include "Component.inl"
