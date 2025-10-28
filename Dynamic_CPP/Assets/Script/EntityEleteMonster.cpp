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

	//����ü ������ �������� //todo : mage ����ü ���̱�
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
	blackBoard->SetValueAsString("State", m_state); //���� ����
	blackBoard->SetValueAsString("Identity", m_identity); //���� ���̵�ƼƼ

	blackBoard->SetValueAsInt("MaxHP", m_maxHP); //�ִ� ü��
	blackBoard->SetValueAsInt("CurrHP", m_currentHP); //���� ü��

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

	//�÷��̾� ��ġ ������Ʈ
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
		 //animatian 2.0f ����
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

		//���� ��ġ �� ȸ��
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		Mathf::Vector3 pos = m_transform->GetWorldPosition();
		Mathf::Vector3 forward = m_transform->GetForward();
		//������ġ
		Mathf::Vector3 startpos = pos + forward * 1.f + Vector3(0, 1.f, 0);

		PrefabObject->SetEnabled(true);
		PrefabObject->m_transform.SetPosition(startpos);
	
		//���� ���Ÿ� ���� ����� ���� forward �������� �������� ����
		//��ũ��Ʈ ���� ����� �Ѱ��ְ� ����� �ϰ� ������
		 MonEleteProjetile* script = PrefabObject->GetComponent<MonEleteProjetile>();
		 script->Initallize(this, m_rangedAttackDamage, m_projectileSpeed,forward);
		 script->m_isMoving = true;
		// ���� �ڿ� �ڽ��� ��� ���¸� disabled �Ѵ�.
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
	m_retreatTreval = 0.0f; // ���� �Ÿ� �ʱ�ȭ
	m_previousPos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
}

void EntityEleteMonster::Retreat(float tick)
{
	//�� ���⼭ ���� ������ �� ����
	//�켱 ������ �ִ� �÷��̾� ��ġ�� �������� 
	Vector3 pos = m_pOwner->GetComponent<Transform>()->GetWorldPosition();
	Vector3 targetPos = target->GetComponent<Transform>()->GetWorldPosition();
	pos.y = 0;
	targetPos.y = 0;
	Quaternion rot = m_pOwner->GetComponent<Transform>()->GetWorldQuaternion();
	//�̵� �����̴� cct ������Ʈ�� �θ���
	CharacterControllerComponent* CCT = m_pOwner->GetComponent<CharacterControllerComponent>();
	
	//����ÿ��� �̵��� �ް����� -> �� �̵����� �ڵ�ȸ�� ���ƾ���
	CCT->SetAutomaticRotation(false);

	//�ϴ� �̵������� �÷��̾��� �ݴ�������� -> �̵� �׷� �켱 �÷��̾���� ������ ���Ѵ�.
	Vector3 dir = targetPos - pos; //->�� ��ƼƼ�κ��� �÷��̾��� ���� ����;
	//������ �����̵� ����� �ϴϱ� 
	//�ϴ� ��ֶ����� �ļ� ���⸸ �����
	dir.Normalize();

	Vector3 desiredDirection = -dir;
	// ��ҿ� ���� �̵� ������, �������� �� ������ ������ �������� Ž���մϴ�.
	Vector3 directionToProbe = m_currentVelocity;
	if (directionToProbe.LengthSquared() < 0.01f) {
		directionToProbe = desiredDirection;
	}
	// �̹� �����ӿ� �̵��� �Ÿ��� �����մϴ�.

	m_previousPos.y = 0;
	Vector3 moveVec = m_previousPos - pos;
	float len = moveVec.LengthSquared();
	m_retreatTreval += len;
	// -- - ���� 1. ���� �̵� �Ÿ� ���� �� ��ǥ ���� Ȯ��(������ ����) ����-- 
	if (m_retreatTreval >= m_retreatDistance)
	{
		// ��ǥ �Ÿ���ŭ ���������Ƿ� ���� ����ϴ�.
		CCT->Move(Vector2::Zero);
		CCT->SetAutomaticRotation(true);
		m_currentVelocity = Vector3::Zero;
		m_isRetreat = false; // ���� ����
		return;
	}

	// 2. �̵� ����� �ӵ��� ���� �������� ������ ����
	std::vector<Feeler> dynamicFeelers = {
		{ m_moveSpeed * 1.5f, {0, 0, 1} },      // ����: ��� (���� �溸)
		{ m_moveSpeed * 0.8f, {0.5f, 0, 0.866f} }, // ���� 30��: ª�� (���� �⵿)
		{ m_moveSpeed * 0.8f, {-0.5f, 0, 0.866f} } // ���� 30��: ª�� (���� �⵿)
	};

	// 3. ObstacleAvoider�� ����� 'ȸ�� ����' ���
	//    ���� �ӵ�(m_currentVelocity)�� �����Ͽ� �̵� ������ �˷��ִ� ���� �ٽ�
	DirectX::SimpleMath::Vector3 avoidanceVector = ObstacleAvoider(
		dynamicFeelers,
		pos,
		rot,
		m_currentVelocity, // ���� �������� ���� �ӵ� = ���� �̵� ����
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

	// --- 5. '����' ���� ���� ---
	if (finalDirection.Dot(desiredDirection) < 0.0f)
	{
		// TODO: ���⿡ '�ٸ� �ൿ'���� ��ȯ�ϴ� ������ �ֽ��ϴ�.
		// ��: BT�� ��� Failure ��ȯ, ���� �ӽ��̸� ChangeState(ATTACK) ��
		CCT->Move(Vector2::Zero); // �ϴ� ����
		CCT->SetAutomaticRotation(true);
		m_currentVelocity = Vector3::Zero;
		return; // Retreat �ൿ ����
	}

	// --- 6. CCT �̵� ���� ---
	Vector2 moveInput = { finalDirection.x,finalDirection.z };
	CCT->Move(moveInput);

	// --- 7. ���� �������� ���� ���� �ӵ� ���� ---
	m_currentVelocity = finalDirection;

	
	
	
	// --- ���� 8. �÷��̾� �ֽ� ȸ�� ���� (�ű� �߰�) ���� ---
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
	//�ڷ���Ʈ ����
	
	std::cout << "StartTeleport " << std::endl;

	// �̹� �ڷ���Ʈ ���̰ų� ��Ÿ���� ���� �ʾ����� �������� �ʽ��ϴ�. -->//BT
	//if (m_isTeleport || blackBoard->GetValueAsFloat("TeleportCooldown") > 0.0f) 
	//{
	//	return;
	//}

	Mathf::Vector3 bestLocation;

	//��ġ ���
	// 1. ������ �ڷ���Ʈ ��ġ�� ����մϴ�.
	if (CalculateTeleport(bestLocation))
	{
		// 2. ��ġ�� ã�Ҵٸ�, �ڷ���Ʈ ������ �����մϴ�.
		m_teleportDestination = bestLocation;
		m_isTeleport = true;
		m_teleportTimer = m_teleportDelay;

		// TODO:����Ʈ ���� telport ������� ����Ʈ
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
		//�浹ü ��Ȱ��ȭ
		GetOwner()->SetLayer("Water"); //�� ���� �ٲٴ°ž� ��������

		////�� ��Ȱ��ȭ
		for (auto child : meshs) {
			if (child) {
				child->SetEnabled(false);
			}
		}
		
		std::cout << "Teleport disabled mesh and layer " << std::endl;
		
		// 1. �������� �̵��մϴ�.
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
		// ������ �ڷ���Ʈ ��ġ�� ã�� ���߽��ϴ�. ��Ÿ���� ª�� ���� ��õ��� �����մϴ�.
		std::cout << "Teleport failde not find location " << std::endl;
		blackBoard->SetValueAsFloat("TeleportCooldown", 1.0f);
	}

	//���� �ð� �ڿ� �ڷ���Ʈ ����
}

void EntityEleteMonster::EndTeleport(float tick)
{
	//// 1. �������� �̵��մϴ�. --> start ���� �̹� �̵�
	//m_pOwner->GetComponent<CharacterControllerComponent>()->ForcedSetPosition(m_teleportDestination);
	//std::cout << "EndTeleport moved ForcedSetPosition " << std::endl;
	
	//// 2. ���͸� �ٽ� ���̰� �ϰ� �浹�� Ȱ��ȭ�մϴ�.
	for (auto child : meshs) {
		if (child) {
			child->SetEnabled(true);
		}
	}

	GetOwner()->SetLayer("Enemy"); 

	std::cout << "Teleport eabled mesh and layer " << std::endl;


	// TODO: ���⿡ ��Ÿ���� ����Ʈ(VFX, SFX) ��� �ڵ带 �߰��մϴ�.
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
	
	// 3. �ֺ��� �о �� �ִ� ������Ʈ���� �о���ϴ�.
	PushToNuisanceObject();
	std::cout << "Teleport PushToNuisanceObject " << std::endl;

	// 4. �ڷ���Ʈ ���¸� �ʱ�ȭ�ϰ� ��Ÿ���� �����մϴ�.
	m_isTeleport = false;
	blackBoard->SetValueAsFloat("TeleportCooldown", m_teleportCoolTime);
	std::cout << "EndTeleport end set TeleportCooldown" << std::endl;
}

/**
 * \brief �ڷ���Ʈ�� ������ ��ġ�� ����Ͽ� ��ȯ�մϴ�.
 * \param bestLocation ã�� ������ ��ġ�� ���� ��¿� ����
 * \param outMonstersToPush �ش� ��ġ�� �̵� �� �о�� �� ���� ���
 * \return ������ ��ġ�� ã������ true, �ƴϸ� false
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
		// 1. �ĺ� ���� ����
		float angle = (float)i / CANDIDATE_COUNT * 2.0f * XM_PI;
		Vector3 dir = { cos(angle), 0, sin(angle) };
		Vector3 candidatePos = selfPos + dir * m_teleportDistance;

		// 2. ��ȿ�� �˻�: ���� ��ֹ��� ��ġ���� Ȯ��
		// ������: "WALL" ���̾�� �����̸�, ���� ������Ʈ�� ���̾� ������ �°� �����ؾ� �մϴ�.
		OverlapInput overlapInput;
		overlapInput.position = candidatePos;
		overlapInput.rotation = Mathf::Quaternion::Identity;
		overlapInput.layerMask = ~0;//LayerMask::WALL;
		std::vector<HitResult> hitResults;
		
		int hitCount = PhysicsManagers->SphereOverlap(overlapInput, monsterRadius, hitResults);

		if (hitCount > 0)
		{
			// ���� ��ֹ��� �������Ƿ� �� ��ġ�� ����� �� �����ϴ�.
			continue;
		}

		// 3. ��ȿ�� ��ġ�̹Ƿ�, �÷��̾�κ��� ���� �� ������ Ȯ���մϴ�.
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

	// 1. ���� �� ��ġ�� �ݰ��� �����ɴϴ�.
	Vector3 currentPos = m_pOwner->m_transform.GetWorldPosition();
	float monsterRadius = cct->GetControllerInfo().radius;

	// 2. �� ��ġ�� �������� Overlap�� �����Ͽ� �о ����� ã���ϴ�.
	OverlapInput overlapInput;
	overlapInput.position = currentPos;
	overlapInput.rotation = Mathf::Quaternion::Identity;
	overlapInput.layerMask = ~0; //LayerMask::MONSTER | LayerMask::PLAYER;
	std::vector<HitResult> hitResults;

	int hitCount = PhysicsManagers->SphereOverlap(overlapInput, monsterRadius, hitResults);

	if (hitCount == 0) return;

	// 3. ã�� ������ �о���ϴ�.
	for (const auto& hit : hitResults)
	{
		GameObject* hitObject = hit.gameObject;
		if (hitObject == m_pOwner) continue; // �ڱ� �ڽ��� ����

		CharacterControllerComponent* otherCCT = hitObject->GetComponent<CharacterControllerComponent>();
		if (otherCCT)
		{
			Vector3 objpos = hitObject->m_transform.GetWorldPosition();
			Vector3 pushDir = objpos - currentPos;
			if (pushDir.LengthSquared() < 0.001f)
			{
				// ��ġ�� ���� �����ִٸ� ������ �������� �о���ϴ�.
				pushDir = { (float)rand() / RAND_MAX * 2.f - 1.f, 0, (float)rand() / RAND_MAX * 2.f - 1.f };
			}
			pushDir.Normalize();
			// ����: 2�� ������ 1�ʰ� �о���ϴ�. �ʿ信 ���� ���� �����ϼ���.
			otherCCT->TriggerForcedMove(pushDir, 0.1f);
		}
	}
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

	//4���� : �÷��̾� �������� ������
	prioritized_directions.push_back(-awayDir);

	for (const auto& dir : prioritized_directions)
	{
		//���� �߰� 4�����Ͻ� �÷��̾� ����
		bool onlast = false;
		if (dir == -awayDir) {
			onlast = true;
		}
		Vector3 candidatePos = pos + dir * 6.0f;

		std::vector<GameObject*> monstersToPush;

		if (IsValidTeleportLocation(candidatePos, monstersToPush, onlast))
		{
			//�ڷ���Ʈ
			PushAndTeleportTo(candidatePos,monstersToPush);
			// TODO: �ڷ���Ʈ ���� �� ��Ÿ�� ���� ���� �߰�
			blackBoard->SetValueAsFloat("TeleportCooldown", m_teleportCoolTime);
			m_isTeleport = false;
			return; // ����
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
					//������2��� hitEffect ũ��Ƽ�� ����Ʈ�� ��� ����,���ҽ� ����
				}
			}
			PlayHitEffect(this->GetOwner(), hitinfo);
	
			//�����忡 ������ ����
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
				DeadEvent(); //Die �ִϸ��̼ǳ����� �ű�� �ű��
			}
			else {
				HitImpulse();
			}
		}
	}
}

bool EntityEleteMonster::IsValidTeleportLocation(const Vector3& candidatePos,  std::vector<GameObject*>& outMonstersToPush, bool onlast)
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
			if (!onlast) {
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
			else {
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


