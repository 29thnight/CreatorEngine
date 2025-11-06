#include "EntityMonsterTower.h"
#include "pch.h"
#include "TestMonsterB.h"
#include "MeshRenderer.h"
#include "PrefabUtility.h"
#include "PlayEffectAll.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
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
		//monsterScript->m_attackRange = attackRange;
		monsterScript->m_projectileRange = attackRange;
		monsterScript->m_chaseRange = attackRange -1.0f;
		monsterScript->m_rangedAttackCoolTime = attackSpeed;
		auto enemyBT = m_pOwner->GetComponent<BehaviorTreeComponent>();
		if (enemyBT)
		{
			auto blackBoard = enemyBT->GetBlackBoard();
			if (blackBoard)
			{
				blackBoard->SetValueAsFloat("ChaseRange", monsterScript->m_chaseRange); // 추적 거리
				//blackBoard->SetValueAsFloat("AttackRange", attackRange); //근접 공격 거리
				blackBoard->SetValueAsFloat("ProjectileRange", attackRange); //투사체 최대 사거리
				blackBoard->SetValueAsFloat("RangedAttackCoolTime", attackSpeed); //원거리 공격 쿨타임
			}
		}
		monsterScript->m_moveSpeed = 0.f;
		if (monsterSpawnPosObj)
		{
			towerMonster->m_transform.SetPosition(monsterSpawnPosObj->m_transform.GetWorldPosition());
		}
	}

	Prefab* deadPrefab = PrefabUtilitys->LoadPrefab("EnemyDeathEffect");
	if (deadPrefab)
	{
		deadObj = PrefabUtilitys->InstantiatePrefab(deadPrefab, "DeadEffect");
		deadObj->SetEnabled(false);
	}

}

void EntityMonsterTower::Update(float tick)
{
	HitImpulseUpdate(tick);
}

void EntityMonsterTower::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{

	if (isDestroy) return;
	HitImpulse();
	m_currentHP -= damage;

	if (towerMonster)
	{
		auto monster = towerMonster->GetComponentDynamicCast<TestMonsterB>();
		if (monster)
		{
			monster->SendDamage(this, 0);

		}

	}

	Entity::SendDamage(sender, damage, hitinfo);

	if (m_currentHP <= 0)
	{
		isDestroy = true;
		SetAlive(false);


		if (deadObj)
		{
			deadObj->SetEnabled(true);
			auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
			Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
			deadPos.y += 0.7f;
			deadObj->GetComponent<Transform>()->SetPosition(deadPos);
			deadEffect->Initialize();
		}

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

