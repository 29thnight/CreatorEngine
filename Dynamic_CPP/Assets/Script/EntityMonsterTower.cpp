#include "EntityMonsterTower.h"
#include "pch.h"
#include "TestMonsterB.h"
#include "MeshRenderer.h"
#include "PrefabUtility.h"
void EntityMonsterTower::Start()
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

		if (childObj->m_tag == posTag)
		{
			monsterSpawnPosObj = childObj;
		}
	}

	if (breakModel)
	{
		auto meshren = breakModel->GetComponentsInchildrenDynamicCast<MeshRenderer>();
		for (auto& m : meshren) {
			m->SetEnabled(false);
		}
	}



	Prefab* monsterPrefab = PrefabUtilitys->LoadPrefab("MonsterB");
	if (monsterPrefab)
	{
		towerMonster = PrefabUtilitys->InstantiatePrefab(monsterPrefab, "towerPrefab");
		auto monsterScript = towerMonster->GetComponentDynamicCast<TestMonsterB>();
		monsterScript->m_attackRange = attackRange;
		monsterScript->m_chaseRange = attackRange;
		if (monsterSpawnPosObj)
		{
			towerMonster->m_transform.SetPosition(monsterSpawnPosObj->m_transform.GetWorldPosition());
		}
	}
}

void EntityMonsterTower::Update(float tick)
{
	HitImpulseUpdate(tick);
}

void EntityMonsterTower::SendDamage(Entity* sender, int damage, HitInfo)
{

	if (isDestroy) return;
	HitImpulse();
	m_currentHP -= damage;

	if (towerMonster)
	{
		towerMonster->GetComponentDynamicCast<TestMonsterB>()->SendDamage(this, 0);
	}


	if (m_currentHP <= 0)
	{
		isDestroy = true;
		SetAlive(false);
		if (normalModel)
		{
			auto meshren = normalModel->GetComponentsInchildrenDynamicCast<MeshRenderer>();
			for (auto& m : meshren) {
				m->SetEnabled(false);
			}
		}
		if (breakModel)
		{
			auto meshren = breakModel->GetComponentsInchildrenDynamicCast<MeshRenderer>();
			for (auto& m : meshren) {
				m->SetEnabled(true);
			}
		}

		GetOwner()->SetLayer("Water");
		if (towerMonster)
		{
			towerMonster->GetComponentDynamicCast<TestMonsterB>()->SendDamage(this,9999);
		}
	}
}

