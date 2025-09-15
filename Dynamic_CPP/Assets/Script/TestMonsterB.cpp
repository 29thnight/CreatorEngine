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

	//����ü ������ ��������
	GameObject* projectilePrefab = GameObject::Find("MonBProjectile");
	if (!projectilePrefab) return;
	//����ü ����
	//GameObject* projectile = projectilePrefab->Clone(); --> Ŭ�� ��� �Ұ� ���� ���� Ȥ�� ������Ʈ Ǯ��
	//Ǯ������ ��������
	//GameObject* projectile = ObjectPool::GetInstance().GetObject(projectilePrefab->GetHashedName()); --> ������Ʈ Ǯ�� ��� �Ұ����� ����
	//�ϴ� �������� ������Ʈ ���� ��������
	projectilePrefab->SetEnabled(true);

	//���� ��ġ �� ȸ��
	Transform* m_transform = m_pOwner->GetComponent<Transform>();
	Mathf::Vector3 pos = m_transform->GetWorldPosition();
	Mathf::Vector3 forward = m_transform->GetForward();

	Mathf::Vector3 startpos = pos + forward * 1.f + Vector3(0, 1.f, 0);
	projectilePrefab->m_transform.SetPosition(startpos);
	//Mathf::Quaternion startrot = Mathf::Quaternion::Identity;
	//projectilePrefab->m_transform.SetRotation(startrot); //��ô ���� ȸ�� ȿ�� �ʿ�� 

	//��ô ���� Ÿ�� ��ġ�� �ִ� ��Ÿ��� ���� ���� ���� ����
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

	//Ŀ�� � ��Ʈ�� ����Ʈ ��� 
	Mathf::Vector3 controllPos = (startpos + endpos) * 0.5f; //�߰���
	controllPos.y += m_projectileArcHeight;

	//�ӵ��� ���� �̵� �ð��� ����Ͽ� ������ Ŀ��� �̵� 
	float estimatedLength = Mathf::Vector3::Distance(startpos, controllPos) + Mathf::Vector3::Distance(controllPos, endpos); //�ٻ� �Ÿ�
	//float fixedSpeed = 30.f; // ���ϴ� ���� �ӵ� (��: �ʴ� 30����) --> m_projectileSpeed
	float calculatedDuration = estimatedLength / m_projectileSpeed;


	//MonsterProjectile* prefabScript projectilePrefab->GetComponent<MonsterProjectile>()
	//prefabScript->Initialize(startPos, controlPos, endPos, calculatedDuration); // ��ũ��Ʈ�� ����Ʈ�� �ð� ����
	// ������ � ���� �� �̵� �浹�� ���� ---> ����ü���� ����
	
}

