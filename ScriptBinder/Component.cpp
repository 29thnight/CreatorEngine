#include "Component.h"
#include "GameObject.h"

Component* Component::GetComponent(HashedGuid typeof)
{
	auto it = m_pOwner->m_componentIds.find(typeof);
	if (it != m_pOwner->m_componentIds.end())
	{
		return m_pOwner->m_components[it->second].get();
	}

	return nullptr;
}
