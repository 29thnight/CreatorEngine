#include "PlayEffectAll.h"
#include "pch.h"
#include "EffectComponent.h"
void PlayEffectAll::Start()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto childObject = GameObject::FindIndex(child);
			if (!childObject) continue;

			auto effectcomponent = childObject->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent->weak_from_this());
			}
		}
	}
	isstart = true;
}

void PlayEffectAll::Update(float tick)
{
	if (m_isCallStart == false)return;
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		for (auto& effect : m_effects)
		{
			auto effectPtr = effect.lock();
			if (effectPtr)
			{
				effectPtr->Apply();
			}
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}


	bool allFinished = true;
	for (auto& effect : m_effects)
	{
		auto effectPtr = effect.lock();
		if (effectPtr && effectPtr->m_isPlaying)
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

void PlayEffectAll::Initialize()
{
	if (m_effects.empty())
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent->weak_from_this());
			}
		}
	}

	for (auto& effect : m_effects)
	{
		auto effectPtr = effect.lock();
		if (effectPtr)
		{
			effectPtr->Apply();
		}
	}
}