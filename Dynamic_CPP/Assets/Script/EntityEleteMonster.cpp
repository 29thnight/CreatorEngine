#include "EntityEleteMonster.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "PrefabUtility.h"

void EntityEleteMonster::Start()
{
	enemyBT = m_pOwner->GetComponent<BehaviorTreeComponent>();
	blackBoard = enemyBT->GetBlackBoard();
	auto childred = m_pOwner->m_childrenIndices;
	for (auto& child : childred)
	{
		auto animator = GameObject::FindIndex(child)->GetComponent<Animator>();

		if (animator)
		{
			m_animator = animator;
			break;
		}

	}

	/*if (!m_animator)
	{
		m_animator = m_pOwner->GetComponent<Animator>();
		auto hitscale = m_animator->GetOwner()->m_transform.scale;
		hitBaseScale = Vector3(hitscale.x, hitscale.y, hitscale.z);
	}
	else {
		auto hitscale = m_animator->GetOwner()->m_transform.scale;
		hitBaseScale = Vector3(hitscale.x, hitscale.y, hitscale.z);
	}*/

	bool hasid = blackBoard->HasKey("Identity");
	if (hasid) {
		std::string id = blackBoard->GetValueAsString("Identity");
		//if (id == "MonsterNomal") {
		//	/*m_animator->SetParameter("Dead", false);
		//	m_animator->SetParameter("Atteck", false);
		//	m_animator->SetParameter("Move", false);*/
		//}
	}

	m_pOwner->m_collisionType = 3;

	for (auto& child : childred)
	{
		auto criticalmark = GameObject::FindIndex(child)->GetComponent<EffectComponent>();

		if (criticalmark)
		{
			markEffect = criticalmark;
			break;
		}

	}

	//����ü ������ �������� //todo : mage ����ü ���̱�
	Prefab* projectilePrefab = PrefabUtilitys->LoadPrefab("MonBProjetile");
	if (projectilePrefab) {
		GameObject* PrefabObject1 = PrefabUtilitys->InstantiatePrefab(projectilePrefab, "MonBProjectile1");
		PrefabObject1->SetEnabled(false);
		GameObject* PrefabObject2 = PrefabUtilitys->InstantiatePrefab(projectilePrefab, "MonBProjectile2");
		PrefabObject2->SetEnabled(false);

		m_projectiles.push_back(PrefabObject1);
		m_projectiles.push_back(PrefabObject2);
	}

	//blackboard initialize
	blackBoard->SetValueAsString("State", m_state); //���� ����
	blackBoard->SetValueAsString("Identity", m_identity); //���� ���̵�ƼƼ

	blackBoard->SetValueAsInt("MaxHP", m_maxHP); //�ִ� ü��
	blackBoard->SetValueAsInt("CurrHP", m_currHP); //���� ü��

	blackBoard->SetValueAsFloat("MoveSpeed", m_moveSpeed); //�̵� �ӵ�
	blackBoard->SetValueAsFloat("ChaseRange", m_chaseRange); // ���� �Ÿ�
	blackBoard->SetValueAsFloat("ChaseOutTime", m_rangeOutDuration); //���� ���� �ð�

	//blackBoard->SetValueAsFloat("AttackRange", m_attackRange); //���� ���� �Ÿ�
	//blackBoard->SetValueAsInt("AttackDamage", m_attackDamage); //���� ���� ������

	blackBoard->SetValueAsInt("RangedAttackDamage", m_rangedAttackDamage); //���Ÿ� ���� ������
	blackBoard->SetValueAsFloat("ProjectileSpeed", m_projectileSpeed); //����ü �ӵ�
	blackBoard->SetValueAsFloat("ProjectileRange", m_projectileRange); //����ü �ִ� ��Ÿ�
	blackBoard->SetValueAsFloat("RangedAttackCoolTime", m_rangedAttackCoolTime); //���Ÿ� ���� ��Ÿ��

	//
	blackBoard->SetValueAsFloat("RetreatDistance", m_retreatDistance);
	blackBoard->SetValueAsFloat("TeleportDistance", m_teleportDistance);
	blackBoard->SetValueAsFloat("TeleportCollTime", m_teleportCollTime);
}

void EntityEleteMonster::Update(float tick)
{
	bool hasIdentity = blackBoard->HasKey("Identity");
	if (hasIdentity) {
		std::string Identity = blackBoard->GetValueAsString("Identity");
	}

	//TPCooldown update
	bool hasTPCooldown = blackBoard->HasKey("TeleportColldown");
	if (hasTPCooldown) 
	{
		float cooldown = blackBoard->GetValueAsFloat("TeleportColldown");
		cooldown -= tick;
		if (cooldown < 0) {
			cooldown = 0.0f;
		}
		blackBoard->SetValueAsFloat("TeleportColldown", cooldown);
	}

	//�÷��̾� ��ġ ������Ʈ
	UpdatePlayer();
}

void EntityEleteMonster::UpdatePlayer()
{
	bool hasAsis = blackBoard->HasKey("Asis");
	bool hasP1 = blackBoard->HasKey("Player1");
	bool hasP2 = blackBoard->HasKey("Player2");

	GameObject* Asis = nullptr;
	if (hasAsis) {
		Asis = blackBoard->GetValueAsGameObject("Asis");
	}
	GameObject* player1 = nullptr;
	if (hasP1) {
		player1 = blackBoard->GetValueAsGameObject("Player1");
	}
	GameObject* player2 = nullptr;
	if (hasP2) {
		player2 = blackBoard->GetValueAsGameObject("Player2");
	}

	Transform* m_transform = m_pOwner->GetComponent<Transform>();
	Mathf::Vector3 pos = m_transform->GetWorldPosition();
	SimpleMath::Vector3 asisPos;
	SimpleMath::Vector3 player1Pos;
	SimpleMath::Vector3 player2Pos;

	float distAsis = FLT_MAX;
	float distPlayer1 = FLT_MAX;
	float distPlayer2 = FLT_MAX;
	if (Asis) {
		asisPos = Asis->m_transform.GetWorldPosition();
		distAsis = Mathf::Vector3::DistanceSquared(asisPos, pos);
	}
	if (player1) {
		player1Pos = player1->m_transform.GetWorldPosition();
		distPlayer1 = Mathf::Vector3::DistanceSquared(player1Pos, pos);
	}
	if (player2) {
		player2Pos = player2->m_transform.GetWorldPosition();
		distPlayer2 = Mathf::Vector3::DistanceSquared(player2Pos, pos);
	}
	GameObject* closedTarget = nullptr;
	float closedDist = FLT_MAX;
	if (distPlayer1 < distPlayer2)
	{
		closedTarget = player1;
		closedDist = distPlayer1;
	}
	else
	{
		closedTarget = player2;
		closedDist = distPlayer2;
	}

	if (isAsisAction) {
		if (distAsis < closedDist)
		{
			closedTarget = Asis;
			closedDist = distAsis;
		}
	}
	if (closedTarget) {
		target = closedTarget;
		blackBoard->SetValueAsGameObject("ClosedTarget", closedTarget->ToString());
		blackBoard->SetValueAsGameObject("Target", closedTarget->ToString());
	}
}

void EntityEleteMonster::ShootingAttack()
{
}

void EntityEleteMonster::ChaseTarget()
{
}

void EntityEleteMonster::Retreat()
{
}

void EntityEleteMonster::Teleport()
{
	//�ڷ���Ʈ ������ �켱������ ������ ���
	std::vector<SimpleMath::Vector3> prioritized_directions;

	Vector3 pos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
	Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();

	// 1����: �÷��̾� ���ݴ� ����
	Vector3 awayDir = pos - targetPos;
	awayDir.y = 0; //���� ���� ����
	awayDir.Normalize();
	prioritized_directions.push_back(awayDir);

	// 2����: �밢�� �� ����
	Quaternion rot45_L = Quaternion::CreateFromAxisAngle(Vector3::Up, XM_PI / 4.0f);
	Quaternion rot45_R = Quaternion::CreateFromAxisAngle(Vector3::Up, -XM_PI / 4.0f);
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot45_L));
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot45_R));

	// 3����: ���� ����
	Quaternion rot90_L = Quaternion::CreateFromAxisAngle(Vector3::Up, XM_PI / 2.0f);
	Quaternion rot90_R = Quaternion::CreateFromAxisAngle(Vector3::Up, -XM_PI / 2.0f);
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot90_L));
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot90_R));

	for (const auto& dir : prioritized_directions)
	{
		Vector3 candidatePos = pos + dir * 6.0f;

		std::vector<GameObject*> monstersToPush;

		if (IsValidTeleportLocation(candidatePos, monstersToPush)) 
		{
			//�ڷ���Ʈ
			PushAndTeleportTo(candidatePos,monstersToPush);
			// TODO: �ڷ���Ʈ ���� �� ��Ÿ�� ���� ���� �߰�
			blackBoard->SetValueAsFloat("TeleportColldown", m_rangedAttackCoolTime);
			m_isTeleport = false;
			return; // ����
		}

	}
}

void EntityEleteMonster::Dead()
{
}

void EntityEleteMonster::RotateToTarget()
{
}

void EntityEleteMonster::SendDamage(Entity* sender, int damage)
{
}

bool EntityEleteMonster::IsValidTeleportLocation(const Vector3& candidatePos,  std::vector<GameObject*>& outMonstersToPush)
{
	// --- �˻� 1: ���� Ȯ�� �� �о ���� Ȯ�� (Overlap) ---
	outMonstersToPush.clear();


	OverlapInput overlapInput;
	overlapInput.position = candidatePos;
	overlapInput.rotation = Quaternion::Identity;
	//overlapInput.layerMask = ~(1 << 10); //���� ���̾� ��ȣ
	std::vector<HitResult> hitResults;

	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();
	float radius = cct->GetControllerInfo().radius;

	int hitCount = PhysicsManagers->SphereOverlap(overlapInput, radius, hitResults);

	if (hitCount > 0)
	{
		for (const auto& hit : hitResults)
		{
			GameObject* hitObject = hit.gameObject; // HitResult ����ü�� gameObject �����Ͱ� �ִٰ� ����

			if (hitObject == m_pOwner) // �ڱ� �ڽ��� ����
			{
				continue;
			}

			// �о �� ���� ������Ʈ(��, �÷��̾� ��)�� �ִٸ� �� ��ġ�� ����
			if (hitObject->m_layer != m_pOwner->m_layer)
			{
				return false;
			}
			// �о ���� ��Ͽ� �߰�
			else
			{
				outMonstersToPush.push_back(hitObject);
			}
		}
	}
	//�ϴ� �ٴ� �˻� ����
	return true;
}

void EntityEleteMonster::PushAndTeleportTo(const Vector3& finalPos, const std::vector<GameObject*>& monstersToPush)
{
	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();

	//���� �о��
	for (auto& monster : monstersToPush) 
	{
		CharacterControllerComponent* otherCCT = monster->GetComponent<CharacterControllerComponent>();
		Transform* monTr = monster->GetComponent<Transform>();
		if (!monTr) {
			continue;
		}
		Mathf::Vector3 monpos = monTr->GetWorldPosition();
		Mathf::Vector3 dir = monpos - finalPos;
		if (otherCCT) {
			if (dir.LengthSquared() < 0.001f)//��ġ�� ������ġ�� ���� ������� ������ �������� ����
			{
				dir = Vector3(static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f, 0, static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);
			}
			dir.Normalize();
			otherCCT->TriggerForcedMove(dir * 2, 1.0f);
		}
	}
	//��ġ �ֺ� ��� ���͸� �о�� ���� �̵�
	if (!m_posset) {
		cct->ForcedSetPosition(finalPos); //�̵� �ǰ� ���� ��� �̵� ��Ű�� ����
		m_posset = true;
	}
}


