#include "EntityEleteMonster.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "MeshRenderer.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "PrefabUtility.h"
#include "MonEleteProjetile.h"
#include "HPBar.h"
#include "EntityAsis.h"
#include "GameManager.h"
#include "CriticalMark.h"
#include "PlayEffectAll.h"
#include "Weapon.h"
struct Feeler {
	float length;
	DirectX::SimpleMath::Vector3 localDirection;
};

void EntityEleteMonster::Start()
{
	m_currentHP = maxHP;
	m_maxHP = maxHP;
	auto canvObj = GameObject::Find("Canvas");
	Prefab* HPBarPrefab = PrefabUtilitys->LoadPrefab("UI_HPBarBg");
	if (HPBarPrefab && canvObj)
	{
		GameObject* hpObj = PrefabUtilitys->InstantiatePrefab(HPBarPrefab, "MonAHp");
		HPBar* hp = hpObj->GetComponent<HPBar>();
		canvObj->AddChild(hpObj);
		hp->targetIndex = GetOwner()->m_index;
		m_currentHP = m_maxHP;
		hp->SetMaxHP(m_maxHP);
		hp->SetCurHP(m_currentHP);
		hp->SetType(0);
		hp->screenOffset = { 0, -100 };
		hp->SetTarget(GetOwner()->shared_from_this());
		hp->Init();
	}
	Prefab* deadPrefab = PrefabUtilitys->LoadPrefab("EnemyDeathEffect");
	if (deadPrefab)
	{
		deadObj = PrefabUtilitys->InstantiatePrefab(deadPrefab, "DeadEffect");
		deadObj->SetEnabled(false);
	}
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
	childred = GetOwner()->m_childrenIndices;
	std::string markTag = "CriticalMark";
	for (auto& child : childred)
	{
		auto Obj = GameObject::FindIndex(child);

		if (Obj->m_tag == markTag)
		{
			m_criticalMark = Obj->GetComponent<CriticalMark>();
			break;
		}

	}

	meshs = m_pOwner->GetComponentsInChildren<MeshRenderer>();

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


	bool hasAsis = blackBoard->HasKey("Asis");
	bool hasP1 = blackBoard->HasKey("Player1");
	bool hasP2 = blackBoard->HasKey("Player2");

	if (hasAsis && !m_asis) {
		m_asis = blackBoard->GetValueAsGameObject("Asis");
	}
	if (hasP1 && !m_player1) {
		m_player1 = blackBoard->GetValueAsGameObject("Player1");
	}
	if (hasP2 && !m_player2) {
		m_player2 = blackBoard->GetValueAsGameObject("Player2");
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
	blackBoard->SetValueAsInt("CurrHP", m_currentHP); //현재 체력

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
	blackBoard->SetValueAsFloat("TeleportCooldown", m_teleportCoolTime);

	HitImpulseStart();
}

void EntityEleteMonster::Update(float tick)
{
	if (blackBoard == nullptr) return;
	bool hasIdentity = blackBoard->HasKey("Identity");
	if (hasIdentity) {
		std::string Identity = blackBoard->GetValueAsString("Identity");
	}

	CharacterControllerComponent* controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetBaseSpeed(m_moveSpeed);
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
		blackBoard->SetValueAsFloat("TeleportCooldown", TPCooltime);
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

	if (m_isTeleport) {
		m_teleportTimer -= tick;
		std::cout << "Teleporting has time : "<< m_teleportTimer << std::endl;
		if (m_teleportTimer <= 0.0f)
		{
			EndTeleport(tick);
		}
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
			if (deadObj)
			{
				deadObj->SetEnabled(true);
				auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
				Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
				deadPos.y += 0.7f;
				deadObj->GetComponent<Transform>()->SetPosition(deadPos);
				deadEffect->Initialize();
			}
		}
	}

	if (isDead)
	{
		deadBugElaspedTime += tick;
		if (deadBugElaspedTime >= deadBugTime)
		{
			if (false == GetOwner()->IsDestroyMark())
			{
				GetOwner()->Destroy();
				if (deadObj)
				{
					deadObj->SetEnabled(true);
					auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
					Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
					deadPos.y += 0.7f;
					deadObj->GetComponent<Transform>()->SetPosition(deadPos);
					deadEffect->Initialize();
				}
			}
		}
	}


	HitImpulseUpdate(tick);

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
	if (!m_asis) {
		m_asis = Asis;
	}

	GameObject* player1 = nullptr;
	if (hasP1) {
		player1 = blackBoard->GetValueAsGameObject("Player1");
	}
	if (!m_player1) {
		m_player1 = player1;
	}
	GameObject* player2 = nullptr;
	if (hasP2) {
		player2 = blackBoard->GetValueAsGameObject("Player2");
	}

	if (!m_player2) {
		m_player1 = player2;
	}

	Transform* m_transform = m_pOwner->GetComponent<Transform>();
	Mathf::Vector3 pos = m_transform->GetWorldPosition();
	SimpleMath::Vector3 asisPos;
	SimpleMath::Vector3 player1Pos;
	SimpleMath::Vector3 player2Pos;

	float distAsis = FLT_MAX;
	float distPlayer1 = FLT_MAX;
	float distPlayer2 = FLT_MAX;

	Player* p1Script = nullptr;
	Player* p2Script = nullptr;

	if (Asis) {
		asisPos = Asis->m_transform.GetWorldPosition();
		distAsis = Mathf::Vector3::DistanceSquared(asisPos, pos);
	}
	if (player1) {
		player1Pos = player1->m_transform.GetWorldPosition();
		p1Script = m_player1->GetComponentDynamicCast<Player>();
		distPlayer1 = Mathf::Vector3::DistanceSquared(player1Pos, pos);
	}
	if (player2) {
		player2Pos = player2->m_transform.GetWorldPosition();
		p2Script = m_player2->GetComponentDynamicCast<Player>();
		distPlayer2 = Mathf::Vector3::DistanceSquared(player2Pos, pos);
	}

	if (p1Script)
	{
		if (p1Script->isStun) distPlayer1 = FLT_MAX;
	}

	if (p2Script)
	{
		if (p2Script->isStun) distPlayer2 = FLT_MAX;
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
		if (closedDist == FLT_MAX) {
			target = nullptr;
			blackBoard->SetValueAsGameObject("ClosedTarget", "");
			blackBoard->SetValueAsGameObject("Target", "");
		}
		else {
			target = closedTarget;
			blackBoard->SetValueAsGameObject("ClosedTarget", closedTarget->ToString());
			blackBoard->SetValueAsGameObject("Target", closedTarget->ToString());
		}
	}
	else {
		target = nullptr;
		blackBoard->SetValueAsGameObject("ClosedTarget", "");
		blackBoard->SetValueAsGameObject("Target", "");
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

void EntityEleteMonster::ChaseTarget(float deltatime)
{
	if (target && !isDead)
	{
		if (m_state == "Attack") return;
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		CharacterControllerComponent* controller = m_pOwner->GetComponent<CharacterControllerComponent>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Transform* targetTransform = target->GetComponent<Transform>();
		if (targetTransform) {

			m_state = "Chase";
			Mathf::Vector3 targetpos = targetTransform->GetWorldPosition();
			Mathf::Vector3 dir = targetpos - pos;
			dir.y = 0.f;

			bool useChaseOutTime = blackBoard->HasKey("ChaseOutTime");
			float outTime = 0.0f;

			if (useChaseOutTime)
			{
				outTime = blackBoard->GetValueAsFloat("ChaseOutTime");
			}

			std::cout << "dist " << dir.Length() << std::endl;

			if (dir.Length() < m_chaseRange)
			{
				outTime = m_rangeOutDuration; // Reset outTime if within range
			}
			else {
				outTime -= deltatime; // Decrease outTime if not within range
			}
			blackBoard->SetValueAsString("State", m_state);
			blackBoard->SetValueAsFloat("ChaseOutTime", outTime);

			dir.Normalize();

			//avoid logic
			Mathf::Vector3 avoidanceForce(0.f, 0.f, 0.f);
			bool obstacleDetected = false;

			Mathf::Vector3 forwardDir = m_transform->GetForward();
			Mathf::Matrix rotLeft = Mathf::Matrix::CreateRotationY(Mathf::ToRadians(-m_avoidanceAngle));
			Mathf::Matrix rotRight = Mathf::Matrix::CreateRotationY(Mathf::ToRadians(m_avoidanceAngle));
			Mathf::Vector3 leftFeelerDir = Mathf::Vector3::TransformNormal(forwardDir, rotLeft);
			Mathf::Vector3 rightFeelerDir = Mathf::Vector3::TransformNormal(forwardDir, rotRight);
			std::vector<Mathf::Vector3> feelerDirs = { forwardDir, leftFeelerDir, rightFeelerDir };

			for (const auto& feelerDir : feelerDirs)
			{
				std::vector<HitResult> res;

				if (RaycastAll(m_transform->GetWorldPosition(), feelerDir, m_obstacleAvoidanceDistance, ~0, res))
				{
					if (res.size() > 0)
					{
						for (auto& hit : res)
						{
							if (hit.gameObject != m_pOwner && hit.gameObject != target)
							{
								obstacleDetected = true;
								avoidanceForce += -feelerDir;
							}
						}
					}
				}
			}
			Mathf::Vector3 finalDir = dir;
			if (obstacleDetected)
			{
				avoidanceForce.Normalize();
				finalDir = dir + avoidanceForce * m_avoidanceSteeringForce;
			}
			finalDir.Normalize();
			//

			if (controller) {
				//look dir
				controller->SetLookDirection({ dir.x, 0.f, dir.z });
				//look dir
				controller->Move({ finalDir.x * m_moveSpeed, finalDir.z * m_moveSpeed });
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
		CCT->SetAutomaticRotation(true);
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
		CCT->SetAutomaticRotation(true);
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
		Vector3 dirToPlayer = pos - targetPos;
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

void EntityEleteMonster::StartTeleport()
{
	//텔레포트 시작
	
	std::cout << "StartTeleport " << std::endl;

	// 이미 텔레포트 중이거나 쿨타임이 차지 않았으면 시작하지 않습니다. -->//BT
	//if (m_isTeleport || blackBoard->GetValueAsFloat("TeleportCooldown") > 0.0f) 
	//{
	//	return;
	//}

	Mathf::Vector3 bestLocation;

	//위치 계산
	// 1. 최적의 텔레포트 위치를 계산합니다.
	if (CalculateTeleport(bestLocation))
	{
		// 2. 위치를 찾았다면, 텔레포트 과정을 시작합니다.
		m_teleportDestination = bestLocation;
		m_isTeleport = true;
		m_teleportTimer = m_teleportDelay;

		// TODO:이펙트 시작 telport 사라지는 이펙트
		Prefab* tpStart = PrefabUtilitys->LoadPrefab("TPstartEffect");
		if (tpStart) {
			GameObject* PrefabObject1 = PrefabUtilitys->InstantiatePrefab(tpStart, "TPstartEffect");
			auto effect = PrefabObject1->GetComponentDynamicCast<PlayEffectAll>();
			PrefabObject1->m_transform.SetPosition(GetOwner()->m_transform.GetWorldPosition());
			if (effect)
			{
				effect->Initialize();
			}
		}
		//충돌체 비활성화
		GetOwner()->SetLayer("Water"); //왜 물로 바꾸는거야 ㅋㅋㅋㅋ

		////모델 비활성화
		for (auto child : meshs) {
			if (child) {
				child->SetEnabled(false);
			}
		}
		
		std::cout << "Teleport disabled mesh and layer " << std::endl;
		
		// 1. 목적지로 이동합니다.
		m_pOwner->GetComponent<CharacterControllerComponent>()->ForcedSetPosition(m_teleportDestination);
		std::cout << "EndTeleport moved ForcedSetPosition " << std::endl;

		Prefab* tpIng = PrefabUtilitys->LoadPrefab("TPingEffect");
		if (tpIng) {
			GameObject* PrefabObject2 = PrefabUtilitys->InstantiatePrefab(tpIng, "TPingEffect");
			auto effect = PrefabObject2->GetComponentDynamicCast<PlayEffectAll>();
			PrefabObject2->m_transform.SetPosition(m_teleportDestination);
			if (effect)
			{
				effect->Initialize();
			}
		}
	}
	else
	{
		// 적절한 텔레포트 위치를 찾지 못했습니다. 쿨타임을 짧게 돌려 재시도를 방지합니다.
		std::cout << "Teleport failde not find location " << std::endl;
		blackBoard->SetValueAsFloat("TeleportCooldown", 1.0f);
	}

	//일정 시간 뒤에 텔레포트 실행
}

void EntityEleteMonster::EndTeleport(float tick)
{
	//// 1. 목적지로 이동합니다. --> start 에서 이미 이동
	//m_pOwner->GetComponent<CharacterControllerComponent>()->ForcedSetPosition(m_teleportDestination);
	//std::cout << "EndTeleport moved ForcedSetPosition " << std::endl;
	
	//// 2. 몬스터를 다시 보이게 하고 충돌을 활성화합니다.
	for (auto child : meshs) {
		if (child) {
			child->SetEnabled(true);
		}
	}

	GetOwner()->SetLayer("Enemy"); 

	std::cout << "Teleport eabled mesh and layer " << std::endl;


	// TODO: 여기에 나타나는 이펙트(VFX, SFX) 재생 코드를 추가합니다.
	Prefab* tpEnd = PrefabUtilitys->LoadPrefab("TPendEffect");
	if (tpEnd) {
		GameObject* PrefabObject1 = PrefabUtilitys->InstantiatePrefab(tpEnd, "TPendEffect");
		auto effect = PrefabObject1->GetComponentDynamicCast<PlayEffectAll>();
		PrefabObject1->m_transform.SetPosition(GetOwner()->m_transform.GetWorldPosition());
		if (effect)
		{
			effect->Initialize();
		}
	}
	
	// 3. 주변의 밀어낼 수 있는 오브젝트들을 밀어냅니다.
	PushToNuisanceObject();
	std::cout << "Teleport PushToNuisanceObject " << std::endl;

	// 4. 텔레포트 상태를 초기화하고 쿨타임을 적용합니다.
	m_isTeleport = false;
	blackBoard->SetValueAsFloat("TeleportCooldown", m_teleportCoolTime);
	std::cout << "EndTeleport end set TeleportCooldown" << std::endl;
}

/**
 * \brief 텔레포트할 최적의 위치를 계산하여 반환합니다.
 * \param bestLocation 찾은 최적의 위치를 담을 출력용 변수
 * \param outMonstersToPush 해당 위치로 이동 시 밀어내야 할 몬스터 목록
 * \return 최적의 위치를 찾았으면 true, 아니면 false
 */
bool EntityEleteMonster::CalculateTeleport(Mathf::Vector3& bestLocation)
{
	const int CANDIDATE_COUNT = 16;
	float maxDistSq = -1.0f;
	bool foundValidLocation = false;

	if (!target) return false;

	Vector3 selfPos = m_pOwner->m_transform.GetWorldPosition();
	Vector3 targetPos = target->m_transform.GetWorldPosition();

	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();
	float monsterRadius = cct->GetControllerInfo().radius;

	for (int i = 0; i < CANDIDATE_COUNT; ++i)
	{
		// 1. 후보 지점 생성
		float angle = (float)i / CANDIDATE_COUNT * 2.0f * XM_PI;
		Vector3 dir = { cos(angle), 0, sin(angle) };
		Vector3 candidatePos = selfPos + dir * m_teleportDistance;

		// 2. 유효성 검사: 고정 장애물과 겹치는지 확인
		// ※주의: "WALL" 레이어는 예시이며, 실제 프로젝트의 레이어 설정에 맞게 수정해야 합니다.
		OverlapInput overlapInput;
		overlapInput.position = candidatePos;
		overlapInput.rotation = Mathf::Quaternion::Identity;
		overlapInput.layerMask = ~0;//LayerMask::WALL;
		std::vector<HitResult> hitResults;
		
		int hitCount = PhysicsManagers->SphereOverlap(overlapInput, monsterRadius, hitResults);

		if (hitCount > 0)
		{
			// 고정 장애물과 겹쳤으므로 이 위치는 사용할 수 없습니다.
			continue;
		}

		// 3. 유효한 위치이므로, 플레이어로부터 가장 먼 곳인지 확인합니다.
		float distSqToPlayer = Vector3::DistanceSquared(candidatePos, targetPos);
		if (distSqToPlayer > maxDistSq)
		{
			maxDistSq = distSqToPlayer;
			bestLocation = candidatePos;
			foundValidLocation = true;
		}
	}

	return foundValidLocation;
}

void EntityEleteMonster::PushToNuisanceObject()
{
	CharacterControllerComponent* cct = m_pOwner->GetComponent<CharacterControllerComponent>();
	if (!cct) return;

	// 1. 현재 내 위치와 반경을 가져옵니다.
	Vector3 currentPos = m_pOwner->m_transform.GetWorldPosition();
	float monsterRadius = cct->GetControllerInfo().radius;

	// 2. 내 위치를 기준으로 Overlap을 실행하여 밀어낼 대상을 찾습니다.
	OverlapInput overlapInput;
	overlapInput.position = currentPos;
	overlapInput.rotation = Mathf::Quaternion::Identity;
	overlapInput.layerMask = ~0; //LayerMask::MONSTER | LayerMask::PLAYER;
	std::vector<HitResult> hitResults;

	int hitCount = PhysicsManagers->SphereOverlap(overlapInput, monsterRadius, hitResults);

	if (hitCount == 0) return;

	// 3. 찾은 대상들을 밀어냅니다.
	for (const auto& hit : hitResults)
	{
		GameObject* hitObject = hit.gameObject;
		if (hitObject == m_pOwner) continue; // 자기 자신은 제외

		CharacterControllerComponent* otherCCT = hitObject->GetComponent<CharacterControllerComponent>();
		if (otherCCT)
		{
			Vector3 objpos = hitObject->m_transform.GetWorldPosition();
			Vector3 pushDir = objpos - currentPos;
			if (pushDir.LengthSquared() < 0.001f)
			{
				// 위치가 거의 겹쳐있다면 임의의 방향으로 밀어냅니다.
				pushDir = { (float)rand() / RAND_MAX * 2.f - 1.f, 0, (float)rand() / RAND_MAX * 2.f - 1.f };
			}
			pushDir.Normalize();
			// 예시: 2의 힘으로 1초간 밀어냅니다. 필요에 따라 값을 조절하세요.
			otherCCT->TriggerForcedMove(pushDir, 0.1f);
		}
	}
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
			blackBoard->SetValueAsFloat("TeleportCooldown", m_teleportCoolTime);
			m_isTeleport = false;
			return; // 성공
		}

	}
}

void EntityEleteMonster::Dead()
{
	m_animator->SetParameter("Dead", true);
	GetOwner()->SetLayer("Water");
	EntityAsis* asisScrip = m_asis->GetComponentDynamicCast<EntityAsis>();
	if (asisScrip) {
		asisScrip->AddPollutionGauge(m_enemyReward);
	}
}

void EntityEleteMonster::DeadEvent()
{
	EndDeadAnimation = true;
	deadObj->SetEnabled(true);
	auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
	Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
	deadPos.y += 0.7f;
	deadObj->GetComponent<Transform>()->SetPosition(deadPos);
	deadEffect->Initialize();
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

void EntityEleteMonster::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	Entity::SendDamage(sender, damage, hitinfo);

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
			if (m_criticalMark)
			{
				if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
				{
					damage *= player->m_curWeapon->coopCrit;
					hitinfo.isCritical = true;
					//데미지2배및 hitEffect 크리티컬 이펙트로 출력 몬스터,리소스 동일
				}
			}
			PlayHitEffect(this->GetOwner(), hitinfo);
	
			//블랙보드에 데미지 저장
			//blackBoard->SetValueAsInt("Damage", damage);
			m_currentHP -= damage;
			blackBoard->SetValueAsInt("CurrHP", m_currentHP);
			//std::cout << "EntityMonsterA SendDamage CurrHP : " << m_currentHP << std::endl;



			

			if (m_currentHP <= 0)
			{
				isDead = true;
				Dead();
				CharacterControllerComponent* controller = m_pOwner->GetComponent<CharacterControllerComponent>();
				controller->Move({ 0, 0 });
				DeadEvent(); //Die 애니메이션나오면 거기로 옮길것
			}
			else {
				HitImpulse();
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


