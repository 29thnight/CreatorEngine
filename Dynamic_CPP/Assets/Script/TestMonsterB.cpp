#include "TestMonsterB.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"

void TestMonsterB::Start()
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
}

void TestMonsterB::Update(float tick)
{
}

void TestMonsterB::Dead()
{
}

void TestMonsterB::ChaseTarget()
{
}

void TestMonsterB::RotateToTarget()
{
}

void TestMonsterB::ShootingAttack()
{
	if (target == nullptr) return;

	//투사체 프리펩 가져오기
	GameObject* projectilePrefab = GameObject::Find("MonBProjectile");
	if (!projectilePrefab) return;
	//투사체 생성
	//GameObject* projectile = projectilePrefab->Clone(); --> 클론 기능 할것 인지 조율 혹은 오브젝트 풀링
	//풀링으로 가져오기
	//GameObject* projectile = ObjectPool::GetInstance().GetObject(projectilePrefab->GetHashedName()); --> 오브젝트 풀링 기능 할것인지 조율
	//일단 만들어놓은 오브젝트 에서 가져오기
	projectilePrefab->SetEnabled(true);

	//생성 위치 및 회전
	Transform* m_transform = m_pOwner->GetComponent<Transform>();
	Mathf::Vector3 pos = m_transform->GetWorldPosition();
	Mathf::Vector3 forward = m_transform->GetForward();

	Mathf::Vector3 startpos = pos + forward * 1.f + Vector3(0, 1.f, 0);
	projectilePrefab->m_transform.SetPosition(startpos);
	//Mathf::Quaternion startrot = Mathf::Quaternion::Identity;
	//projectilePrefab->m_transform.SetRotation(startrot); //투척 이후 회전 효과 필요시 

	//투척 직적 타겟 위치와 최대 사거리에 따른 낙하 지점 고정
	Mathf::Vector3 endpos;
	Mathf::Vector3 targetpos = target->m_transform.GetWorldPosition();
	float distance = Mathf::Vector3::Distance(pos, targetpos);
	if (m_projectileRange < distance) {
		Mathf::Vector3 dir = targetpos - pos;
		dir.Normalize();
		endpos = pos + dir * m_projectileRange;
	}
	else
	{
		endpos = targetpos;
	}

	//커브 곡선 컨트롤 포인트 계산 
	Mathf::Vector3 controllPos = (startpos + endpos) * 0.5f; //중간점
	controllPos.y += m_projectileArcHeight;

	//속도는 고정 이동 시간을 계산하여 베지어 커브로 이동 
	float estimatedLength = Mathf::Vector3::Distance(startpos, controllPos) + Mathf::Vector3::Distance(controllPos, endpos); //근사 거리
	//float fixedSpeed = 30.f; // 원하는 고정 속도 (예: 초당 30미터) --> m_projectileSpeed
	float calculatedDuration = estimatedLength / m_projectileSpeed;


	//MonsterProjectile* prefabScript projectilePrefab->GetComponent<MonsterProjectile>()
	//prefabScript->Initialize(startPos, controlPos, endPos, calculatedDuration); // 스크립트에 포인트와 시간 전달
	// 베지에 곡선 생성 및 이동 충돌시 폭발 ---> 투사체에서 실행
	
}

