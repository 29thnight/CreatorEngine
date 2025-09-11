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
	blackBoard->SetValueAsString("State", m_state);
	blackBoard->SetValueAsString("Identity", m_identity);
	blackBoard->SetValueAsFloat("MoveSpeed", m_moveSpeed);
	blackBoard->SetValueAsInt("MaxHP", m_maxHP);
	blackBoard->SetValueAsInt("CurrHP", m_currHP);
	blackBoard->SetValueAsFloat("AttackRange", m_attackRange);
	blackBoard->SetValueAsInt("AttackDamage", m_attackDamage);
	blackBoard->SetValueAsFloat("ChaseRange", m_chaseRange);
	blackBoard->SetValueAsFloat("ChaseOutTime", m_rangeOutDuration);
}

void EntityMonsterA::Update(float tick)
{
	GameObject* Asis = blackBoard->GetValueAsGameObject("Asis");
	GameObject* player1 = blackBoard->GetValueAsGameObject("Player1");
	GameObject* player2 = blackBoard->GetValueAsGameObject("Player2");
	
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

	if (isAttack) {
		m_state = "Attack";
		if (!isAttackAnimation) {
			isAttackAnimation = true;
			m_animator->SetParameter("Attack", true);	
		}
		else {
			bool haskey = blackBoard->HasKey("IsAttacking");
			if (haskey) {
				isAttackAnimation = blackBoard->GetValueAsBool("IsAttacking");
			}
		}
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

}

void EntityMonsterA::AttackBoxOn()
{
	std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
}

void EntityMonsterA::AttackBoxOff()
{
	std::cout << "EntityMonsterA AttackBoxOn" << std::endl;
}

void EntityMonsterA::ChaseTarget()
{
	if (target && !isDead)
	{
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
}

void EntityMonsterA::SendDamage(Entity* sender, int damage)
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
			Mathf::Vector3 p = XMVector3Rotate(dir * m_knockBackVelocity, XMQuaternionInverse(m_animator->GetOwner()->m_transform.GetWorldQuaternion()));
			hittimer = m_MaxknockBackTime;
			hitPos = p;
			m_animator->GetOwner()->m_transform.SetScale(hitBaseScale * m_knockBackScaleVelocity);
			



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
			}
		}
	}
}

