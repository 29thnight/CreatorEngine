#include "SlashEffect.h"
#include "pch.h"
#include "EffectComponent.h"
void SlashEffect::Start()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;
		auto temp = GetOwner()->GetComponent<EffectComponent>();
		if(temp)
			m_effects.push_back(temp);

		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent);
			}
		}
	}
	 isstart = true;
}

void SlashEffect::Update(float tick)
{
	if (m_isCallStart == false)return;

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


	//들고있는 이펙트 재생끝나면 알아서 풀로 들어가게끔 
	/*if (m_effect->m_isPlaying == false)
	{
		GetOwner()->Destroy();
	}*/
}

void SlashEffect::Initialize()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;

		auto temp = GetOwner()->GetComponent<EffectComponent>();
		if (temp)
			m_effects.push_back(temp);

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
	beLateFrame = false;
	OnEffect = false;
}

