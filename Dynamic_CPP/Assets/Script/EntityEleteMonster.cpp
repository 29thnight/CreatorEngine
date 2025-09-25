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
#include "MonEleteProjetile.h"

struct Feeler {
	float length;
	DirectX::SimpleMath::Vector3 localDirection;
};

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
	CharacterControllerComponent* controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetAutomaticRotation(true);
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
	Prefab* projectilePrefab = PrefabUtilitys->LoadPrefab("MonEleteProjectile");
	if (projectilePrefab) {
		GameObject* PrefabObject1 = PrefabUtilitys->InstantiatePrefab(projectilePrefab, "MonEleteProjectile1");
		PrefabObject1->SetEnabled(false);
		GameObject* PrefabObject2 = PrefabUtilitys->InstantiatePrefab(projectilePrefab, "MonEleteProjectile2");
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
	blackBoard->SetValueAsFloat("RetreatRange", m_retreatRange);
	blackBoard->SetValueAsFloat("TeleportDistance", m_teleportDistance);
	blackBoard->SetValueAsFloat("TeleportCollTime", m_teleportCoolTime);
}

void EntityEleteMonster::Update(float tick)
{
	bool hasIdentity = blackBoard->HasKey("Identity");
	if (hasIdentity) {
		std::string Identity = blackBoard->GetValueAsString("Identity");
	}

	//TPCooldown update
	bool hasTPCooldown = blackBoard->HasKey("TeleportCooldown");
	bool hasRTCooldown = blackBoard->HasKey("ReteatCooldown");
	float TPCooltime = 0.0f;
	float RTCooltime = 0.0f;
	if (hasTPCooldown) 
	{
		TPCooltime = blackBoard->GetValueAsFloat("TeleportCooldown");
		TPCooltime -= tick;
		if (TPCooltime < 0) {
			TPCooltime = 0.0f;
		}
		blackBoard->SetValueAsFloat("TeleportColldown", TPCooltime);
	}
	if (hasRTCooldown)
	{
		RTCooltime = blackBoard->GetValueAsFloat("ReteatCooldown");
		RTCooltime -= tick;
		if (RTCooltime < 0) {
			RTCooltime = 0.0f;
		}
		blackBoard->SetValueAsFloat("ReteatCooldown", RTCooltime);
	}

	//플레이어 위치 업데이트
	UpdatePlayer();

	if (m_isRetreat) {
		Retreat(tick);
	}

	if (isAttack) {
		m_state = "Attack";
	}

	
	if (EndDeadAnimation)
	{
		deadElapsedTime += tick;
		if (deadDestroyTime <= deadElapsedTime)
		{
			GetOwner()->Destroy();
		}
	}


	bool haskey = blackBoard->HasKey("IsAttacking");
	if (haskey) {
		isAttackAnimation = blackBoard->GetValueAsBool("IsAttacking");
		//test
		 //animatian 2.0f 진행
		static float time = 0.0f;
		time += tick;
		if (time > 2.0f) {
			isAttackAnimation = false;
			time = 0.0f;
		}
		//
	}

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
	if (target == nullptr) return;

	GameObject* PrefabObject = m_projectiles[m_projectileIndex];
	if (PrefabObject) {
		bool isUsed = PrefabObject->IsEnabled();
		if (isUsed) return;

		//생성 위치 및 회전
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Mathf::Vector3 forward = m_transform->GetForward();
		//생성위치
		Mathf::Vector3 startpos = pos + forward * 1.f + Vector3(0, 1.f, 0);

		PrefabObject->SetEnabled(true);
		PrefabObject->m_transform.SetPosition(startpos);
	
		//기존 원거리 공격 방식을 말고 forward 방향으로 직선으로 날림
		//스크립트 따로 만들고 넘겨주고 직선운동 하게 변경함
		 MonEleteProjetile* script = PrefabObject->GetComponent<MonEleteProjetile>();
		 script->Initallize(this, m_rangedAttackDamage, m_projectileSpeed,forward);
		 script->m_isMoving = true;
		// 폭파 뒤엔 자신의 사용 상태를 disabled 한다.
		m_projectileIndex++;
		if (m_projectileIndex >= m_projectiles.size()) {
			m_projectileIndex = 0;
		}
	}
}

void EntityEleteMonster::ChaseTarget()
{
	if (target && !isDead)
	{
		if (m_state == "Attack") return;
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		CharacterControllerComponent* controller = m_pOwner->GetComponent<CharacterControllerComponent>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Transform* targetTransform = target->GetComponent<Transform>();
		if (targetTransform) {
			Mathf::Vector3 targetpos = targetTransform->GetWorldPosition();
			Mathf::Vector3 dir = targetpos - pos;
			dir.y = 0.f;
			dir.Normalize();

			if (controller) {
				controller->Move({ dir.x * m_moveSpeed, dir.z * m_moveSpeed });
				m_state = "Chase";
			}


		}
	}
}

void EntityEleteMonster::StartRetreat()
{
	m_isRetreat = true;
	m_retreatTreval = 0.0f; // 누적 거리 초기화
	m_previousPos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
}

void EntityEleteMonster::Retreat(float tick)
{
	//자 여기서 부터 도망을 쳐 보자
	//우선 근접해 있는 플레이어 위치를 가져오자 
	Vector3 pos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
	Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();
	pos.y = 0;
	targetPos.y = 0;
	Quaternion rot = m_pOwner->GetComponent<Transform>()->GetWorldQuaternion();
	//이동 관려이니 cct 컴포넌트를 부르고
	CharacterControllerComponent* CCT = m_pOwner->GetComponent<CharacterControllerComponent>();
	
	//후퇴시에는 이동시 뒷걸음질 -> 즉 이동방향 자동회전 막아야함
	CCT->SetAutomaticRotation(false);

	//일단 이동방향은 플레이어의 반대방향으로 -> 이동 그럼 우선 플레이어로의 방향을 구한다.
	Vector3 dir = targetPos - pos; //->이 엔티티로부터 플레이어의 방향 백터;
	//점프나 상하이동 없어야 하니까 
	//일단 노멀라이즈 쳐서 방향만 남기고
	dir.Normalize();

	Vector3 desiredDirection = -dir;
	// 평소엔 현재 이동 방향을, 멈춰있을 땐 가려는 방향을 기준으로 탐색합니다.
	Vector3 directionToProbe = m_currentVelocity;
	if (directionToProbe.LengthSquared() < 0.01f) {
		directionToProbe = desiredDirection;
	}
	// 이번 프레임에 이동한 거리를 누적합니다.

	m_previousPos.y = 0;
	Vector3 moveVec = m_previousPos - pos;
	float len = moveVec.LengthSquared();
	m_retreatTreval += len;
	// -- - ▼▼▼ 1. 실제 이동 거리 누적 및 목표 도달 확인(수정된 로직) ▼▼▼-- 
	if (m_retreatTreval >= m_retreatDistance)
	{
		// 목표 거리만큼 도망쳤으므로 후퇴를 멈춥니다.
		CCT->Move(Vector2::Zero);
		m_currentVelocity = Vector3::Zero;
		m_isRetreat = false; // 상태 변경
		return;
	}

	// 2. 이동 방향과 속도에 따라 동적으로 더듬이 구성
	std::vector<Feeler> dynamicFeelers = {
		{ m_moveSpeed * 1.5f, {0, 0, 1} },      // 정면: 길게 (조기 경보)
		{ m_moveSpeed * 0.8f, {0.5f, 0, 0.866f} }, // 우측 30도: 짧게 (정밀 기동)
		{ m_moveSpeed * 0.8f, {-0.5f, 0, 0.866f} } // 좌측 30도: 짧게 (정밀 기동)
	};

	// 3. ObstacleAvoider를 사용해 '회피 벡터' 계산
	//    현재 속도(m_currentVelocity)를 전달하여 이동 방향을 알려주는 것이 핵심
	DirectX::SimpleMath::Vector3 avoidanceVector = ObstacleAvoider(
		dynamicFeelers,
		pos,
		rot,
		m_currentVelocity, // 이전 프레임의 최종 속도 = 현재 이동 방향
		CCT->m_radius,
		CCT->m_height,
		m_avoidanceStrength,
		~0
	);

	Vector3 desiredForce = desiredDirection;
	Vector3 finalForce = desiredForce + avoidanceVector;

	Vector3 finalDirection;
	if (finalForce.LengthSquared() < 0.001f) {
		finalDirection = (m_currentVelocity.LengthSquared() > 0.01f) ? m_currentVelocity : Vector3::Zero;
	}
	else {
		finalDirection = finalForce;
		finalDirection.Normalize();
	}

	// --- 5. '갇힘' 상태 감지 ---
	if (finalDirection.Dot(desiredDirection) < 0.0f)
	{
		// TODO: 여기에 '다른 행동'으로 전환하는 로직을 넣습니다.
		// 예: BT의 경우 Failure 반환, 상태 머신이면 ChangeState(ATTACK) 등
		CCT->Move(Vector2::Zero); // 일단 멈춤
		m_currentVelocity = Vector3::Zero;
		return; // Retreat 행동 종료
	}

	// --- 6. CCT 이동 실행 ---
	Vector2 moveInput = { finalDirection.x,finalDirection.z };
	CCT->Move(moveInput);

	// --- 7. 다음 프레임을 위해 현재 속도 갱신 ---
	m_currentVelocity = finalDirection;

	
	
	
	// --- ▼▼▼ 8. 플레이어 주시 회전 로직 (신규 추가) ▼▼▼ ---
	{
		Vector3 dirToPlayer = targetPos - pos;
		dirToPlayer.y = 0;
		dirToPlayer.Normalize();

		Matrix lookAtMatrix = Matrix::CreateLookAt(Vector3::Zero, dirToPlayer, Vector3::Up);
		Quaternion targetRotation = Quaternion::CreateFromRotationMatrix(lookAtMatrix.Invert());

		float rotationSpeed = 10.0f;
		Quaternion newRotation = Quaternion::Slerp(rot, targetRotation, tick * rotationSpeed);

		m_pOwner->GetComponent<Transform>()->SetRotation(newRotation);
	}
}

DirectX::SimpleMath::Vector3 EntityEleteMonster::ObstacleAvoider(
	const std::vector<Feeler>& feelers,
	const DirectX::SimpleMath::Vector3& currentPosition,
	const DirectX::SimpleMath::Quaternion& currentOrientation,
	const DirectX::SimpleMath::Vector3& currentVelocity,
	float characterShapeRadius,
	float characterShapeHalfHeight,
	float avoidanceStrength,
	unsigned int layerMask
)
{
	if (currentVelocity.LengthSquared() < 0.01f || feelers.empty()) {
		return DirectX::SimpleMath::Vector3::Zero;
	}

	DirectX::SimpleMath::Vector3 movementDirection = currentVelocity;
	movementDirection.Normalize();
	DirectX::SimpleMath::Quaternion movementOrientation = 
		DirectX::SimpleMath::Quaternion::LookRotation(movementDirection, DirectX::SimpleMath::Vector3::Up);

	HitResult mostThreateningHit;
	bool obstacleFound = false;
	float maxFeelerLength = 0.f;

	for (const auto& feeler : feelers)
	{
		SweepInput input;
		input.startPosition = currentPosition;
		input.startRotation = currentOrientation;
		input.direction = DirectX::SimpleMath::Vector3::Transform(feeler.localDirection, movementOrientation);
		input.distance = feeler.length;
		input.layerMask = layerMask;

		std::vector<HitResult> hits;
		PhysicsManagers->CapsuleSweep(input, characterShapeRadius, characterShapeHalfHeight, hits);

		if (!hits.empty())
		{
			HitResult closestHit_in_feeler = hits[0];
			for (size_t i = 1; i < hits.size(); ++i) {
				if (hits[i].distance < closestHit_in_feeler.distance) {
					closestHit_in_feeler = hits[i];
				}
			}

			if (!obstacleFound || closestHit_in_feeler.distance < mostThreateningHit.distance)
			{
				mostThreateningHit = closestHit_in_feeler;
				obstacleFound = true;
				if (feeler.length > maxFeelerLength) maxFeelerLength = feeler.length;
			}
		}
	}

	if (obstacleFound)
	{
		DirectX::SimpleMath::Vector3 avoidanceDir = mostThreateningHit.normal;
		float strengthScale = (maxFeelerLength - mostThreateningHit.distance) / maxFeelerLength;
		return avoidanceDir * avoidanceStrength * strengthScale;
	}

	return DirectX::SimpleMath::Vector3::Zero;

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

	//4순위 : 플레이어 방향으로 역으로
	prioritized_directions.push_back(-awayDir);

	for (const auto& dir : prioritized_directions)
	{
		//변수 추가 4순위일시 플레이어 무시
		bool onlast = false;
		if (dir == -awayDir) {
			onlast = true;
		}
		Vector3 candidatePos = pos + dir * 6.0f;

		std::vector<GameObject*> monstersToPush;

		if (IsValidTeleportLocation(candidatePos, monstersToPush, onlast))
		{
			//텔레포트
			PushAndTeleportTo(candidatePos,monstersToPush);
			// TODO: 텔레포트 성공 후 쿨타임 적용 로직 추가
			blackBoard->SetValueAsFloat("TeleportCooldown", m_rangedAttackCoolTime);
			m_isTeleport = false;
			return; // 성공
		}

	}
}

void EntityEleteMonster::Dead()
{
	m_animator->SetParameter("Dead", true);
	GetOwner()->SetLayer("Water");
}

void EntityEleteMonster::DeadEvent()
{
	EndDeadAnimation = true;
}

void EntityEleteMonster::RotateToTarget()
{
	SimpleMath::Quaternion rot = m_pOwner->m_transform.GetWorldQuaternion();
	if (target)
	{
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Transform* targetTransform = target->GetComponent<Transform>();
		if (targetTransform) {
			Mathf::Vector3 targetpos = targetTransform->GetWorldPosition();
			Mathf::Vector3 dir = targetpos - pos;
			dir.y = 0.f;
			dir.Normalize();
			SimpleMath::Quaternion lookRot = SimpleMath::Quaternion::CreateFromRotationMatrix(SimpleMath::Matrix::CreateLookAt(Mathf::Vector3::Zero, -dir, Mathf::Vector3::Up));
			lookRot.Inverse(lookRot);
			//rot = SimpleMath::Quaternion::Slerp(rot, lookRot, 0.2f);
			//m_pOwner->m_transform.SetRotation(rot);
			m_pOwner->m_transform.SetRotation(lookRot);
		}
	}
}

void EntityEleteMonster::SendDamage(Entity* sender, int damage)
{
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		//CurrHP - damae;
		if (player)
		{
			Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 senderPos = sender->GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = curPos - senderPos;

			dir.Normalize();

			/* 몬스터 흔들리는 이펙트 MonsterNomal은 에니메이션 대체
			*/
			/*Mathf::Vector3 p = XMVector3Rotate(dir * m_knockBackVelocity, XMQuaternionInverse(m_animator->GetOwner()->m_transform.GetWorldQuaternion()));
			hittimer = m_MaxknockBackTime;
			hitPos = p;
			m_animator->GetOwner()->m_transform.SetScale(hitBaseScale * m_knockBackScaleVelocity);*/




			//블랙보드에 데미지 저장
			//blackBoard->SetValueAsInt("Damage", damage);
			m_currentHP -= damage;
			blackBoard->SetValueAsInt("CurrHP", m_currentHP);
			//std::cout << "EntityMonsterA SendDamage CurrHP : " << m_currentHP << std::endl;



			/*크리티컬 마크 이펙트
			int playerIndex = player->playerIndex;
			m_currentHP -= std::max(damage, 0);
			if (true == criticalMark.TryCriticalHit(playerIndex))
			{
				if (markEffect)
				{
					if (criticalMark.markIndex == 0)
					{
						markEffect->PlayEffectByName("red");
					}
					else if (criticalMark.markIndex == 1)
					{
						markEffect->PlayEffectByName("blue");
					}
					else
					{
						markEffect->StopEffect();
					}
				}
			}*/


			if (m_currentHP <= 0)
			{
				isDead = true;
				Dead();
				DeadEvent(); //Die 애니메이션나오면 거기로 옮길것
			}
		}
	}
}

bool EntityEleteMonster::IsValidTeleportLocation(const Vector3& candidatePos,  std::vector<GameObject*>& outMonstersToPush, bool onlast)
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
			if (!onlast) {
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
			else {
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


