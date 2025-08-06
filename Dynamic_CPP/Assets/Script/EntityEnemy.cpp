#include "EntityEnemy.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
void EntityEnemy::Start()
{
	enemyBT =GetOwner()->GetComponent<BehaviorTreeComponent>();
	blackBoard = enemyBT->GetBlackBoard();

}

void EntityEnemy::Update(float tick)
{
	Mathf::Vector3 forward = GetOwner()->m_transform.GetForward();
	std::cout << "Enemy Forward: " << forward.x << " " << forward.y << " " << forward.z << std::endl;

	if (criticalMark != CriticalMark::None)
	{
		if (criticalMark == CriticalMark::P1)
		{
			auto obj = GameObject::Find("red");
			Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
			pos.y += 5;
			obj->m_transform.SetPosition(pos);
			obj->m_transform.UpdateWorldMatrix();
			auto effect = obj->GetComponent<EffectComponent>();
			if (effect)
			{
				effect->PlayEffectByName("red");
			}
		}
		else if (criticalMark == CriticalMark::P2)
		{

		}
	}

	attackCount = blackBoard->GetValueAsInt("AttackCount");

	if (attackCount > 0) {
		MeleeAttack();
	}


	if (isDead)
	{
		//effect
		Destroy();
	}
}

void EntityEnemy::SetCriticalMark(int playerIndex)
{
	if (playerIndex == 0)
	{
		criticalMark = CriticalMark::P1;
	}
	else if (playerIndex == 1)
	{
		criticalMark = CriticalMark::P2;
	}
}

void EntityEnemy::Attack(Entity* sender, int damage)
{

	if (sender)
	{
		auto player = dynamic_cast<Player*>(sender);
		//CurrHP - damae;
		if (player)
		{
			blackBoard->SetValueAsInt("Damage", damage);
			int playerIndex = player->playerIndex;
			m_currentHP -= std::max(damage, 0);
			if (m_currentHP >= 0)
			{
				isDead = true;
			}
		}
	}
}

void EntityEnemy::MeleeAttack()
{
	if (isDead) return;
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	//bool hasDir = blackBoard->HasKey("AttackDirection");
	//Mathf::Vector3 dir = hasDir ? blackBoard->GetValueAsVector3("AttackDirection") : Mathf::Vector3::Zero;
	//dir.z = -dir.z; // z�� ����, z���� ������ ������
	//dir.y = 0.f; // y���� �����ϰ� ���� �������θ� ����
	//transform forward base ���߿� ����
	Mathf::Vector3 forward = GetOwner()->m_transform.GetForward();
	//pos += forward * 2.f;
	//�ɸ��� ���� ��� �⺻ ���� 0.5�� �Ǵ�
	pos.y = 0.5f; // ray cast height 
	//forward.y = 0.5f; // ray cast height

	//debug��
	//Mathf::Vector3 endpos = pos + forward * 10.f;
	//std::cout << "Start Pos: " << pos.x << " " << pos.y << " " << pos.z << std::endl;
	//forward.Normalize();
	//std::cout << forward.x << " " << forward.y << " " << forward.z << std::endl;
	
	//std::cout << "End Pos: " << endpos.x << " " << endpos.y << " " << endpos.z << std::endl;


	

	std::vector<HitResult> hits;
	int size = RaycastAll(pos,forward, 2.f, 1u, hits);
	/*if (dir != Mathf::Vector3::Zero)
	{
		dir.Normalize();
	}
	else
	{
		dir = GetOwner()->m_transform.GetForward();
	}*/
	//UINT layerMask = 0xffffffff; // Assuming layer 0 is the player layer
	//int size = RaycastAll(pos, dir, 100.f, ~0, hits);

	//std::cout << dir.x << " " << dir.y << " " << dir.z << std::endl;
	std::cout << "Hit Count: " << size << std::endl;
	
	for (auto& hit : hits)
	{
		auto object = hit.hitObject;
		if (object == GetOwner()) continue;
		std::cout << object->m_name.data() << std::endl;

		//todo : �˾Ƽ� �ٲټ� player ���� Ȯ�� �ϰ� �������� �ֵ� �˾Ƽ��ϼ�
		Player* player = object->GetComponent<Player>();
		if (player)
		{
			player->Attack(this, 0);
		}
	}
	
	attackCount -= 1;
	blackBoard->SetValueAsInt("AttackCount", attackCount);
}


