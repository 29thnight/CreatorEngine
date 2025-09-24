#include "EntityMonsterA.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"

void EntityMonsterA::Start()
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
	if (!m_animator)
	{
		m_animator = m_pOwner->GetComponent<Animator>();
		auto hitscale = m_animator->GetOwner()->m_transform.scale;
		hitBaseScale = Vector3(hitscale.x, hitscale.y, hitscale.z);
	}
	else {
		auto hitscale = m_animator->GetOwner()->m_transform.scale;
		hitBaseScale = Vector3(hitscale.x, hitscale.y, hitscale.z);
	}

	bool hasid = blackBoard->HasKey("Identity");
	if (hasid) {
		std::string id = blackBoard->GetValueAsString("Identity");
		if (id == "MonsterNomal") {
			/*m_animator->SetParameter("Dead", false);
			m_animator->SetParameter("Atteck", false);
			m_animator->SetParameter("Move", false);*/
		}
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

	//blackboard initialize
	blackBoard->SetValueAsString("State", m_state); //현제 상태
	blackBoard->SetValueAsString("Identity", m_identity); //고유 아이덴티티

	blackBoard->SetValueAsInt("MaxHP", m_maxHP); //최대 체력
	blackBoard->SetValueAsInt("CurrHP", m_currentHP); //현재 체력

	blackBoard->SetValueAsFloat("MoveSpeed", m_moveSpeed); //이동 속도
	blackBoard->SetValueAsFloat("ChaseRange", m_chaseRange); // 추적 거리
	blackBoard->SetValueAsFloat("ChaseOutTime", m_rangeOutDuration); //추적 지속 시간

	blackBoard->SetValueAsFloat("AttackRange", m_attackRange); //근접 공격 거리
	blackBoard->SetValueAsInt("AttackDamage", m_attackDamage); //근접 공격 데미지
}

void EntityMonsterA::Update(float tick)
{

	if (blackBoard == nullptr)
	{
		return;
	}
	bool hasAsis = blackBoard->HasKey("Asis");
	bool hasP1 = blackBoard->HasKey("Player1");
	bool hasP2 = blackBoard->HasKey("Player2");

	GameObject* Asis = nullptr;
	if (hasAsis) {
		Asis = blackBoard->GetValueAsGameObject("Asis");
	}
	GameObject* player1 = nullptr;
	if (hasP1){
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

	if (isBoxAttack) {
		SweepInput sweepInput;
		sweepInput.startPosition = pos + Vector3(0, 0.5f, 0);
		sweepInput.startRotation = SimpleMath::Quaternion::Identity;
		sweepInput.distance = m_attackRange;
		sweepInput.direction = m_pOwner->m_transform.GetForward();
		sweepInput.layerMask = 1 << 5 | 1 << 7;
		SimpleMath::Vector3 boxHalfExtents = { 0.5f,0.5f,0.5f };
		std::vector<HitResult> hitResults;
		int count = PhysicsManagers->BoxSweep(sweepInput, boxHalfExtents, hitResults);
		if (count > 0) {
			for (auto& hit : hitResults) {
				std::cout << "EntityMonsterA AttackBoxOn hit : " << hit.gameObject->GetHashedName().ToString() << std::endl;
				if (hit.gameObject->GetHashedName().ToString() == player1->GetHashedName().ToString()
					|| hit.gameObject->GetHashedName().ToString()== player2->GetHashedName().ToString()) //player
				{
					std::cout << "EntityMonsterA AttackBoxOn SendDamage : " << hit.gameObject->GetHashedName().ToString() << std::endl;
					auto entity = hit.gameObject->GetComponentDynamicCast<Entity>();
					if (entity) {
						entity->SendDamage(this, m_attackDamage);	
					}
				}
			}
		}
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
	}
	

	if (m_state == "Chase") {
		m_animator->SetParameter("Move", true);
	}
	else {
		m_animator->SetParameter("Move", false);
	}

	//피격 흔들림 처리
	if (hittimer > 0.f) {
		hittimer -= tick;
		if (hittimer < 0.f) hittimer = 0.f;
		m_animator->GetOwner()->m_transform.SetPosition(Mathf::Vector3::Lerp(Mathf::Vector3::Zero, hitPos, hittimer / m_MaxknockBackTime));
		m_animator->GetOwner()->m_transform.SetScale(Mathf::Vector3::Lerp(hitBaseScale, hitBaseScale * m_knockBackScaleVelocity, hittimer / m_MaxknockBackTime));
	}

	// test code start
	if (InputManagement->IsKeyDown('M'))
	{
		GetOwner()->Destroy();
	}
	// test code end
}



void EntityMonsterA::AttackBoxOn()
{
	std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
	isBoxAttack = true;
}

void EntityMonsterA::AttackBoxOff()
{
	std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
	isBoxAttack = false;
}

void EntityMonsterA::ChaseTarget()
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

void EntityMonsterA::Dead()
{
	m_animator->SetParameter("Dead", true);
	GetOwner()->SetLayer("Water");
	//todo : Dead entity remove or disable
}


void EntityMonsterA::DeadEvent()
{
	EndDeadAnimation = true;
	//GetOwner()->Destroy(); //&&&&&풀에 넣기
	//monster death Effect 생성
}

void EntityMonsterA::RotateToTarget()
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

void EntityMonsterA::SendDamage(Entity* sender, int damage)
{
	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 senderPos = sender->GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = curPos - senderPos;

			dir.Normalize();

			/* 몬스터 흔들리는 이펙트 MonsterNomal은 에니메이션 대체
			*/
			Mathf::Vector3 p = XMVector3Rotate(dir * m_knockBackVelocity, XMQuaternionInverse(m_animator->GetOwner()->m_transform.GetWorldQuaternion()));
			hittimer = m_MaxknockBackTime;
			hitPos = p;
			m_animator->GetOwner()->m_transform.SetScale(hitBaseScale * m_knockBackScaleVelocity);
			



			//블랙보드에 데미지 저장
			//blackBoard->SetValueAsInt("Damage", damage);
			m_currentHP -= damage;
			blackBoard->SetValueAsInt("CurrHP", m_currentHP);
			//std::cout << "EntityMonsterA SendDamage CurrHP : " << m_currentHP << std::endl;



			


			if (m_currentHP <= 0)
			{
				isDead = true;
				Dead();
			}
		}
	}
}

