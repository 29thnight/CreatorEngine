#include "EntityMonsterBaseGate.h"
#include "RigidBodyComponent.h"
#include "BoxColliderComponent.h"
#include "pch.h"
#include "MeshRenderer.h"
void EntityMonsterBaseGate::Start()
{
	m_maxHP = maxHP;
	m_currentHP = maxHP;

	
	HitImpulseStart();

	auto childIndexs = GetOwner()->m_childrenIndices;

	for (auto& child : childIndexs)
	{
		auto childObj = GameObject::FindIndex(child);
		if (childObj->m_tag == normalTag)
		{
			normalModel = childObj;
		}
		if (childObj->m_tag == breakTag)
		{
			breakModel = childObj;
		}
	}

	if (breakModel)
	{
		breakModel->GetComponent<MeshRenderer>()->SetEnabled(false);
	}

}

void EntityMonsterBaseGate::Update(float tick)
{
	HitImpulseUpdate(tick);
}

void EntityMonsterBaseGate::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	if (isDestroy) return;
	HitImpulse();
	m_currentHP -= damage;

	if (m_currentHP <= 0)
	{
		isDestroy = true;
		SetAlive(false);

		if (normalModel)
		{
			normalModel->GetComponent<MeshRenderer>()->SetEnabled(false);
		}
		if (breakModel)
		{
			breakModel->GetComponent<MeshRenderer>()->SetEnabled(true);
		}

		GetOwner()->SetLayer("Water");

	}
}


