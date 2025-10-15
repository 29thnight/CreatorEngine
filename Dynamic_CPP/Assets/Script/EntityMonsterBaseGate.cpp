#include "EntityMonsterBaseGate.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "pch.h"
void EntityMonsterBaseGate::Start()
{
	m_maxHP = maxHP;
	m_currentHP = maxHP;
	HitImpulseStart();
}

void EntityMonsterBaseGate::Update(float tick)
{
	HitImpulseUpdate(tick);
}

void EntityMonsterBaseGate::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	HitImpulse();
}


