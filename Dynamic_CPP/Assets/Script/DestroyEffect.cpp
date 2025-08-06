#include "DestroyEffect.h"
#include "pch.h"
#include "EffectComponent.h"

void DestroyEffect::Start()
{
	m_effectComp = GetOwner()->GetComponent<EffectComponent>();
	m_effectComp->Apply();
}

void DestroyEffect::Update(float tick)
{
	m_currentT += tick;
	if (m_currentT > m_effectComp->m_duration)
	{
		GetOwner()->Destroy();
	}
}

