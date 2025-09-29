#include "SwordHitEffect.h"
#include "pch.h"
#include "EffectComponent.h"
void SwordHitEffect::Start()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent);
			}
		}
	}

	
}

void SwordHitEffect::Update(float tick)
{
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		for (auto& effect : m_effects)
		{
			effect->Apply();
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}


	bool allFinished = true;
	for (auto& effect : m_effects)
	{
		if (effect->m_isPlaying)  
		{
			allFinished = false;
			break;
		}
	}

	if (allFinished)
	{
		GetOwner()->Destroy();
	}
}

void SwordHitEffect::Initialize()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent);
			}
		}
	}

	for (auto& effect : m_effects)
	{
		effect->Apply();
	}
}

