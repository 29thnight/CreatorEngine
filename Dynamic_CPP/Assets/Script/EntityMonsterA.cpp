#include "EntityMonsterA.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "SwordHitEffect.h"
#include "PrefabUtility.h"
#include "HPBar.h"
#include "CriticalMark.h"
#include "EntityAsis.h"

#include "PlayEffectAll.h"
#include "GameManager.h"
#include "Weapon.h"
#include "TimeSystem.h"
void EntityMonsterA::Start()
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


	if (m_criticalMark == nullptr)
	{
		Prefab* criticalMarkPre = PrefabUtilitys->LoadPrefab("CriticalMark");
		{
			if (criticalMarkPre)
			{
				auto Obj = PrefabUtilitys->InstantiatePrefab(criticalMarkPre, "criticalMark");
				m_criticalMark = Obj->GetComponent<CriticalMark>();
				GetOwner()->AddChild(Obj);
			}
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



	GameObject* GMObj = GameObject::Find("GameManager");
	GameManager* GM = nullptr;
	if (GMObj)
	{
		 GM = GMObj->GetComponent<GameManager>();
	}
	//blackboard initialize
	blackBoard->SetValueAsString("State", m_state); //���� ����
	blackBoard->SetValueAsString("Identity", m_identity); //���� ���̵�ƼƼ

	blackBoard->SetValueAsInt("MaxHP", m_maxHP); //�ִ� ü��
	blackBoard->SetValueAsInt("CurrHP", m_currentHP); //���� ü��

	blackBoard->SetValueAsFloat("MoveSpeed", m_moveSpeed); //�̵� �ӵ�
	blackBoard->SetValueAsFloat("ChaseRange", m_chaseRange); // ���� �Ÿ�
	blackBoard->SetValueAsFloat("ChaseOutTime", m_rangeOutDuration); //���� ���� �ð�

	blackBoard->SetValueAsFloat("AtkRange", m_attackRange); //���� ���� �Ÿ�
	blackBoard->SetValueAsInt("AttackDamage", m_attackDamage); //���� ���� ������

	if (GM)
	{
		//std::vector<Entity*> players = GM->GetPlayers();
		//if(!players.empty())
		//	blackBoard->SetValueAsString("Player1", players[0]->GetOwner()->ToString());
		//if (players.size() >= 2)
		//{
		//	blackBoard->SetValueAsString("Player2", players[1]->GetOwner()->ToString());
		//}
	}
	bool hasAsis = blackBoard->HasKey("Asis");
	bool hasP1 = blackBoard->HasKey("Player1");
	bool hasP2 = blackBoard->HasKey("Player2");

	if (hasAsis) {
		m_asis = blackBoard->GetValueAsGameObject("Asis");
	}
	if (hasP1) {
		m_player1 = blackBoard->GetValueAsGameObject("Player1");
	}
	if (hasP2) {
		m_player2 = blackBoard->GetValueAsGameObject("Player2");
	}
	HitImpulseStart();
}

void EntityMonsterA::Update(float tick)
{
	if (blackBoard == nullptr)
	{
		return;
	}

	CharacterControllerComponent* controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	controller->SetBaseSpeed(m_moveSpeed);

	bool hasAsis = blackBoard->HasKey("Asis");
	bool hasP1 = blackBoard->HasKey("Player1");
	bool hasP2 = blackBoard->HasKey("Player2");

	if (hasAsis && !m_asis) {
		m_asis = blackBoard->GetValueAsGameObject("Asis");
	}
	if (hasP1 && !m_player1){
		m_player1 = blackBoard->GetValueAsGameObject("Player1");
	}
	if (hasP2 && !m_player2) {
		m_player2 = blackBoard->GetValueAsGameObject("Player2");
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

	if (m_asis) {
		asisPos = m_asis->m_transform.GetWorldPosition();
		distAsis = Mathf::Vector3::DistanceSquared(asisPos, pos);
	}
	if (m_player1) {
		player1Pos = m_player1->m_transform.GetWorldPosition();
		p1Script = m_player1->GetComponentDynamicCast<Player>();
		distPlayer1 = Mathf::Vector3::DistanceSquared(player1Pos, pos);
	}
	if (m_player2) {
		player2Pos = m_player2->m_transform.GetWorldPosition();
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
		closedTarget = m_player1;
		closedDist = distPlayer1;
	}
	else 
	{
		closedTarget = m_player2;
		closedDist = distPlayer2;
	}

	if (isAsisAction) {
		if (distAsis < closedDist)
		{
			closedTarget = m_asis;
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
				//std::cout << "EntityMonsterA AttackBoxOn hit : " << hit.gameObject->GetHashedName().ToString() << std::endl;
				if (hit.gameObject->GetHashedName() == m_player1->GetHashedName()
					|| hit.gameObject->GetHashedName() == m_player2->GetHashedName()
					|| hit.gameObject->GetHashedName() == m_asis->GetHashedName()) //player
				{
					//std::cout << "EntityMonsterA AttackBoxOn SendDamage : " << hit.gameObject->GetHashedName().ToString() << std::endl;
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


	bool haskey = blackBoard->HasKey("IsAttacking");
	if (haskey) {
		isAttackAnimation = blackBoard->GetValueAsBool("IsAttacking");
	}
	
	Benchmark bm;
	if (m_state == "Chase") {
		m_animator->SetParameter("Move", true);
	}
	else {
		m_animator->SetParameter("Move", false);
	}
	std::cout << "MonsterA State Time : " << bm.GetElapsedTime() << std::endl;

	//�ǰ� ��鸲 ó��
	if (hittimer > 0.f) {
		hittimer -= tick;
		if (hittimer < 0.f) hittimer = 0.f;
		m_animator->GetOwner()->m_transform.SetPosition(Mathf::Vector3::Lerp(Mathf::Vector3::Zero, hitPos, hittimer / m_MaxknockBackTime));
		m_animator->GetOwner()->m_transform.SetScale(Mathf::Vector3::Lerp(hitBaseScale, hitBaseScale * m_knockBackScaleVelocity, hittimer / m_MaxknockBackTime));
	}

	HitImpulseUpdate(tick);

	// test code start
	if (InputManagement->IsKeyDown('M'))
	{
		GetOwner()->Destroy();
	}
	// test code end
}

void EntityMonsterA::SetStagger(float time)
{
	auto anim = GetOwner()->GetComponentsInChildren<Animator>();
	for (auto& a : anim) {
		a->StopAnimation(time);
	}
}



void EntityMonsterA::AttackBoxOn()
{
	//std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
	isBoxAttack = true;
}

void EntityMonsterA::AttackBoxOff()
{
	//std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
	isBoxAttack = false;
}

void EntityMonsterA::ChaseTarget(float deltatime)
{
	if (target && !isDead)
	{
		if (m_state == "Attack") return;
		Transform* m_transform = m_pOwner->GetComponent<Transform>();
		CharacterControllerComponent* controller = m_pOwner->GetComponent<CharacterControllerComponent>();
		controller->SetAutomaticRotation(true);
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

			//std::cout << "dist "<< dir.Length() << std::endl;

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

void EntityMonsterA::Dead()
{
	m_animator->SetParameter("Dead", true);
	GetOwner()->SetLayer("Water");
	//todo : Dead entity remove or disable
	EntityAsis* asisScrip = m_asis->GetComponentDynamicCast<EntityAsis>();
	if (asisScrip) {
		asisScrip->AddPollutionGauge(m_enemyReward);
	}
}


void EntityMonsterA::DeadEvent()
{
	EndDeadAnimation = true;

	/*if (deadObj)
	{
		deadObj->SetEnabled(true);
		auto deadEffect = deadObj->GetComponent<PlayEffectAll>();
		Mathf::Vector3 deadPos = GetOwner()->m_transform.GetWorldPosition();
		deadPos.y += 0.7f;
		deadObj->GetComponent<Transform>()->SetPosition(deadPos);
		deadEffect->Initialize();
	}*/
	//GetOwner()->Destroy(); //&&&&&Ǯ�� �ֱ�
	//monster death Effect ����
}

void EntityMonsterA::RotateToTarget()
{
	if (isDead) return;
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

void EntityMonsterA::SendDamage(Entity* sender, int damage, HitInfo hitinfo)
{
	Entity::SendDamage(sender, damage, hitinfo);

	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		if (player)
		{
			Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 senderPos = sender->GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = curPos - senderPos;

			if (m_criticalMark)
			{
				if (true == m_criticalMark->UpdateMark(static_cast<int>(player->m_playerType)))
				{
					damage *= player->m_curWeapon->coopCrit;
					hitinfo.isCritical = true;
					//������2��� hitEffect ũ��Ƽ�� ����Ʈ�� ��� ����,���ҽ� ����
				}
			}
			dir.Normalize();

			/* ���� ��鸮�� ����Ʈ MonsterNomal�� ���ϸ��̼� ��ü
			*/
			Mathf::Vector3 p = XMVector3Rotate(dir * m_knockBackVelocity, XMQuaternionInverse(m_animator->GetOwner()->m_transform.GetWorldQuaternion()));
			hittimer = m_MaxknockBackTime;
			hitPos = p;
			m_animator->GetOwner()->m_transform.SetScale(hitBaseScale * m_knockBackScaleVelocity);
			



			//�����忡 ������ ����
			//blackBoard->SetValueAsInt("Damage", damage);
			m_currentHP -= damage;
			blackBoard->SetValueAsInt("CurrHP", m_currentHP);
			//std::cout << "EntityMonsterA SendDamage CurrHP : " << m_currentHP << std::endl;


			
			PlayHitEffect(this->GetOwner(), hitinfo);


			if (m_currentHP <= 0)
			{
				//Time->SetTimeScale(0.1f,5.0f);
				isDead = true;
				Dead();
				CharacterControllerComponent* controller = m_pOwner->GetComponent<CharacterControllerComponent>();
				controller->Move({ 0, 0 });
			}
			else {
				HitImpulse();
			}
		}
	}
}

