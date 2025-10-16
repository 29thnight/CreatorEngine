#include "SwordProjectileEffect.h"
#include "pch.h"
#include "EffectComponent.h"
#include "SceneManager.h"
#include "InputManager.h"

void SwordProjectileEffect::Start()
{
	GetOwner()->GetComponent<EffectComponent>()->Apply();

	for (auto obj : GetOwner()->m_childrenIndices)
	{
		auto effectcomponent = GameObject::FindIndex(obj)->GetComponent<EffectComponent>();

		effectcomponent->Apply();
	}
}

void SwordProjectileEffect::Update(float tick)
{
}

