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
	auto transform = GetOwner()->GetComponent<Transform>();

	m_time += tick;
	float offset = sin(m_time * 10.0f) * 10.0f;
	transform->SetPosition({ offset, 0, 0 });
}

