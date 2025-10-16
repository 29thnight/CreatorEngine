#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"

constexpr size_t floatSize = sizeof(float);

void Entity::HitImpulseStart()
{
	auto meshren = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	for (auto& m : meshren) {
		m->m_Material = m->m_Material->Instantiate(m->m_Material);
	}
}

void Entity::HitImpulse()
{
	m_currentHitImpulseDuration = m_maxHitImpulseDuration;
}

void Entity::HitImpulseUpdate(float tick)
{
	if (m_currentHitImpulseDuration < 0.f) return;

	m_currentHitImpulseDuration -= tick;

	auto meshren = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	float ratio = m_currentHitImpulseDuration / m_maxHitImpulseDuration;
	for (auto& m : meshren) {
		m->m_Material->TrySetValue("ImpulseScale", "maxImpulse", &m_maxHitImpulseSize, floatSize);
		m->m_Material->TrySetValue("ImpulseScale", "lerpValue", &ratio, floatSize);
		m->m_Material->TrySetValue("FlashBuffer", "flashStrength", &ratio, floatSize);
		m->m_Material->TrySetValue("FlashBuffer", "flashFrequency", &m_hitImpulseFrequency, floatSize);
	}
}
