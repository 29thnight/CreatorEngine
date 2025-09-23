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

	//투사체 프리펩 가져오기 //todo : mage 투사체 붙이기
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
	blackBoard->SetValueAsString("State", m_state); //현제 상태
	blackBoard->SetValueAsString("Identity", m_identity); //고유 아이덴티티

	blackBoard->SetValueAsInt("MaxHP", m_maxHP); //최대 체력
	blackBoard->SetValueAsInt("CurrHP", m_currHP); //현재 체력

	blackBoard->SetValueAsFloat("MoveSpeed", m_moveSpeed); //이동 속도
	blackBoard->SetValueAsFloat("ChaseRange", m_chaseRange); // 추적 거리
	blackBoard->SetValueAsFloat("ChaseOutTime", m_rangeOutDuration); //추적 지속 시간

	//blackBoard->SetValueAsFloat("AttackRange", m_attackRange); //근접 공격 거리
	//blackBoard->SetValueAsInt("AttackDamage", m_attackDamage); //근접 공격 데미지

	blackBoard->SetValueAsInt("RangedAttackDamage", m_rangedAttackDamage); //원거리 공격 데미지
	blackBoard->SetValueAsFloat("ProjectileSpeed", m_projectileSpeed); //투사체 속도
	blackBoard->SetValueAsFloat("ProjectileRange", m_projectileRange); //투사체 최대 사거리
	blackBoard->SetValueAsFloat("RangedAttackCoolTime", m_rangedAttackCoolTime); //원거리 공격 쿨타임

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

	//플레이어 위치 업데이트
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
	//텔레포트 방향을 우선순위로 저장할 목록
	std::vector<SimpleMath::Vector3> prioritized_directions;

	Vector3 pos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
	Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();

	// 1순위: 플레이어 정반대 방향
	Vector3 awayDir = pos - targetPos;
	awayDir.y = 0; //높이 방향 무시
	awayDir.Normalize();
	prioritized_directions.push_back(awayDir);

	// 2순위: 대각선 뒤 방향
	Quaternion rot45_L = Quaternion::CreateFromAxisAngle(Vector3::Up, XM_PI / 4.0f);
	Quaternion rot45_R = Quaternion::CreateFromAxisAngle(Vector3::Up, -XM_PI / 4.0f);
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot45_L));
	prioritized_directions.push_back(Vector3::Transform(awayDir, rot45_R));

	// 3순위: 측면 방향
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
			//텔레포트
			PushAndTeleportTo(candidatePos,monstersToPush);
			// TODO: 텔레포트 성공 후 쿨타임 적용 로직 추가
			blackBoard->SetValueAsFloat("TeleportColldown", m_rangedAttackCoolTime);
			m_isTeleport = false;
			return; // 성공
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
	// --- 검사 1: 공간 확보 및 밀어낼 몬스터 확인 (Overlap) ---
	outMonstersToPush.clear();


	OverlapInput overlapInput;
	overlapInput.position = candidatePos;
	overlapInput.rotation = Quaternion::Identity;
	//overlapInput.layerMask = ~(1 << 10); //몬스터 레이어 번호
	std::vector<HitResult> hitResults;

	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();
	float radius = cct->GetControllerInfo().radius;

	int hitCount = PhysicsManagers->SphereOverlap(overlapInput, radius, hitResults);

	if (hitCount > 0)
	{
		for (const auto& hit : hitResults)
		{
			GameObject* hitObject = hit.gameObject; // HitResult 구조체에 gameObject 포인터가 있다고 가정

			if (hitObject == m_pOwner) // 자기 자신은 무시
			{
				continue;
			}

			// 밀어낼 수 없는 오브젝트(벽, 플레이어 등)가 있다면 이 위치는 실패
			if (hitObject->m_layer != m_pOwner->m_layer)
			{
				return false;
			}
			// 밀어낼 몬스터 목록에 추가
			else
			{
				outMonstersToPush.push_back(hitObject);
			}
		}
	}
	//일단 바닥 검사 안함
	return true;
}

void EntityEleteMonster::PushAndTeleportTo(const Vector3& finalPos, const std::vector<GameObject*>& monstersToPush)
{
	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();

	//몬스터 밀어내기
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
			if (dir.LengthSquared() < 0.001f)//위치가 텔포위치와 거의 같은경우 랜덤한 방향으로 날림
			{
				dir = Vector3(static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f, 0, static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f);
			}
			dir.Normalize();
			otherCCT->TriggerForcedMove(dir * 2, 1.0f);
		}
	}
	//위치 주변 모든 몬스터를 밀어내고 나서 이동
	if (!m_posset) {
		cct->ForcedSetPosition(finalPos); //이동 되고 나서 계속 이동 시키면 낭비
		m_posset = true;
	}
}


