#include "PortalTrigger.h"
#include "pch.h"
#include "ClearPortal.h"
#include "EntityAsis.h"
void PortalTrigger::Start()
{

	auto children = GetOwner()->m_childrenIndices;
	for (auto child : children)
	{
		GameObject* childObj = GameObject::FindIndex(child);
		
		if (childObj)
		{
			portal = childObj->GetComponent<ClearPortal>();
			if (portal)
				break;
		}
	}
}

void PortalTrigger::OnTriggerEnter(const Collision& collision)
{
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();
	if (asis)
	{
		if (portal)
		{
			if(portal->isPortalReady == false)
				portal->isPortalReady = true;
		}
	}
}

void PortalTrigger::Update(float tick)
{

}

