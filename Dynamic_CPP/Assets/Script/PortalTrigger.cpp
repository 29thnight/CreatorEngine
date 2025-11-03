#include "PortalTrigger.h"
#include "pch.h"
#include "ClearPortal.h"
#include "EntityAsis.h"
#include "EffectComponent.h"
#include "TutorialUI.h"
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
			if (portal->isPortalReady == false)
			{
				portal->isPortalReady = true;
				portal->m_portalEffect->Apply();
				if (portal->m_tutorialUi)
				{
					portal->m_tutorialUi->GetOwner()->SetEnabled(true);
				}
			}
		}
	}
}

void PortalTrigger::Update(float tick)
{

}

