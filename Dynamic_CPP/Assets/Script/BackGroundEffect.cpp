#include "EntityAsis.h"
#include "EffectComponent.h"
#include "PrefabUtility.h"
#include "BackGroundEffect.h"
#include "pch.h"

void BackGroundEffect::Start()
{
	m_Effect = GameObject::Find("BackGroundEffect");

	if (!m_Effect)
	{
		return;
	}

	m_Effect->GetComponent<EffectComponent>()->Apply();
}

void BackGroundEffect::OnTriggerEnter(const Collision& collision)
{
	EntityAsis* asis = collision.otherObj->GetComponent<EntityAsis>();

	if (asis)
	{
		if (m_Effect)
		{
			m_Effect->GetComponent<EffectComponent>()->ChangeEffect("background2");
		}
	}
}

void BackGroundEffect::Update(float tick)
{

}

