#include "Component.h"
#include "GameObject.h"

void Component::SetOwner(GameObject* owner)
{
	m_pOwner = owner; 
	m_pTransform = &m_pOwner->m_transform;
}

Component& Component::GetComponent(HashedGuid typeof)
{
	if (!m_pOwner) throw std::null_exception("not set owner");

	// K2: m_componentIds(맵) 소멸 — GameObject::FindComponent(공개 창구, 마스크
	// 선판정 + 선형 탐색)로 수렴.
	if (Component* found = m_pOwner->FindComponent(typeof))
	{
		return *found;
	}

	throw std::null_exception("Component not found");
}