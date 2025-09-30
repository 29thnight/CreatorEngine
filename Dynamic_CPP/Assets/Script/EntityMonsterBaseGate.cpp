#include "EntityMonsterBaseGate.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "pch.h"
void EntityMonsterBaseGate::Start()
{
	m_maxHP = maxHP;
	m_currentHP = maxHP;

}

void EntityMonsterBaseGate::Update(float tick)
{
}

void EntityMonsterBaseGate::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	
}


