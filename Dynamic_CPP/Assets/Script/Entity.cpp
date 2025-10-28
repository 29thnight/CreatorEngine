#include "Entity.h"
#include "MeshRenderer.h"
#include "Material.h"
#include "Animator.h"
#include "Player.h"
#include "EventTarget.h"

constexpr size_t floatSize = sizeof(float);

void Entity::SendDamage(Entity* sender, int damage, HitInfo)
{
	if (!sender) return;

	auto player = dynamic_cast<Player*>(sender);
	if (player)
	{
		int playerIndex = player->playerIndex;
		auto eventTarget = GetOwner()->GetComponent<EventTarget>();
		if (eventTarget)
		{
			eventTarget->playerID = playerIndex;
		}
	}
}

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

void Entity::UpdateOutLine(float tick)
{
	if (OnOutline == true)
	{
		detectElapsedTime += tick;
		if (detectElapsedTime >= detectTime)
		{
			OnOutline = false;
			detectElapsedTime = 0;
			auto meshren = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
			for (auto& m : meshren)
			{
				m->m_bitflag = 0;
			}
		}
	}
}

void Entity::OnOutLine()
{
	auto meshren = GetOwner()->GetComponentsInchildrenDynamicCast<MeshRenderer>();
	for (auto& m : meshren)
	{
		m->m_bitflag = 1;
	}
	OnOutline = true;
}

void Entity::SetAlive(bool isAlive)
{
	IsAlive = isAlive;
}

bool Entity::GetAlive()
{
	return IsAlive;
}

void Entity::StopHitImpulse()
{
	m_currentHitImpulseDuration = 0.f;
}

void Entity::SetStagger(float time)
{
	auto anim = GetOwner()->GetComponentsInChildren<Animator>();
	for (auto& a : anim) {
		a->StopAnimation(time);
	}
}
