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

	auto it = m_pOwner->m_componentIds.find(typeof);
	if (it != m_pOwner->m_componentIds.end())
	{
		return *m_pOwner->m_components[it->second].get();
	}

	throw std::null_exception("Component not found");
}