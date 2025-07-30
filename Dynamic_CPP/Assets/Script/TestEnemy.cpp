#include "TestEnemy.h"
#include "RigidBodyComponent.h"
#include "pch.h"
#include "GameManager.h"
#include "Player.h"

void TestEnemy::Start()
{
	Owner = GetOwner();
	auto rigid = Owner->GetComponent<RigidBodyComponent>();
	rigid->LockAngularXYZ();
	rigid->LockLinearXZ();

	gm = GameObject::Find("GameManager")->GetComponent<GameManager>();
}

void TestEnemy::Update(float tick)
{
	if (!gm) return;

	targetLostTimer -= tick;
	if(targetLostTimer <= 0.f) {
		directionToTarget = Mathf::Vector3::Zero;
	}

	// asis, player순으로 탐색하고 마지막인 player가 우선순위 높음. (이러면 나중에 등록된 player가 우선순위 높음)
	auto players = gm->GetPlayers();
	auto asis = gm->GetAsis();
	Mathf::Vector3 enemyPos = Owner->m_transform.GetWorldPosition();
	for (auto& a : asis) {
		Mathf::Vector3 asisPos = a->GetOwner()->m_transform.GetWorldPosition();
		if (Mathf::Vector3::DistanceSquared(asisPos, enemyPos) < detectRange * detectRange) {
			targetLostTimer = maxTargetLostTimer;
			directionToTarget = asisPos - enemyPos;
			directionToTarget.Normalize();
		}
	}

	for (auto& p : players) {
		Mathf::Vector3 playerPos = p->GetOwner()->m_transform.GetWorldPosition();

		if (Mathf::Vector3::DistanceSquared(playerPos, enemyPos) < detectRange * detectRange) {
			targetLostTimer = maxTargetLostTimer;
			directionToTarget = playerPos - enemyPos;
			directionToTarget.Normalize();
		}
	}
}

