#include "EntityEnemy.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
void EntityEnemy::Start()
{
	enemy = GetOwner();
	enemyBT = enemy->GetComponent<BehaviorTreeComponent>();
	blackBoard = enemyBT->GetBlackBoard();
	auto childred = enemy->m_childrenIndices;
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
		m_animator = enemy->GetComponent<Animator>();
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
}

void EntityEnemy::Update(float tick)
{
	Mathf::Vector3 forward = enemy->m_transform.GetForward();
	//std::cout << "Enemy Forward: " << forward.x << " " << forward.y << " " << forward.z << std::endl;

	attackCount = blackBoard->GetValueAsInt("AttackCount");

	//MeleeAttack();
	//if (isKnockBack)
	//{
	//	KnockBackElapsedTime += tick;
	//	if (KnockBackElapsedTime >= KnockBackTime)
	//	{
	//		isKnockBack = false;
	//		KnockBackElapsedTime = 0.f;
	//		GetOwner()->GetComponent<CharacterControllerComponent>()->EndKnockBack();
	//	}
	//	else
	//	{
	//		auto forward = GetOwner()->m_transform.GetForward(); //���� ���⿡�� �и��Բ� ����
	//		auto controller = GetOwner()->GetComponent<CharacterControllerComponent>();
	//		controller->Move({ forward.x ,forward.z });

	//	}
	//}
	if (isDead)
	{
		//effect
		//Destroy();
	}

	if (markEffect)
	{
		//����Ʈ ��ġ�� ������Ʈ�� ȸ���� ������� ī�޶��ʿ��� ���̰Բ� �ű��
	}
}



void EntityEnemy::SendDamage(Entity* sender, int damage)
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
			}

			
			if (m_currentHP <= 0)
			{
				isDead = true;
			}
		}
	}
}

void EntityEnemy::SendKnockBack(Entity* sender, Mathf::Vector2 KnockBackForce)
{
	if (sender)
	{

		Player* _player = dynamic_cast<Player*>(sender);
		if (_player)
		{
			isKnockBack = true;
			KnockBackTime = 0.1f;
			GetOwner()->GetComponent<CharacterControllerComponent>()->SetKnockBack(KnockBackForce.x, KnockBackForce.y);
		}
	}
}

void EntityEnemy::MeleeAttack()
{
	if (isDead) return;
	Mathf::Vector3 pos = GetOwner()->m_transform.GetWorldPosition();
	bool hasDir = blackBoard->HasKey("AttackDirection");
	Mathf::Vector3 dir;
	if (!hasDir) {
		return; // ���� ������ ������ ����
	}
	dir = blackBoard->GetValueAsVector3("AttackDirection");

	//dir.z = -dir.z; // z�� ����, z���� ������ ������
	dir.y = 0.f; // y���� �����ϰ� ���� �������θ� ����
	//transform forward base ���߿� ����
	//Mathf::Vector3 forward = GetOwner()->m_transform.GetForward();
	//pos += forward * 2.f;
	//�ɸ��� ���� ��� �⺻ ���� 0.5�� �Ǵ�

	//������ ���� ���̹��� 2�� �߰�
	Mathf::Vector3 dir1 = dir;
	Mathf::Vector3 dir2 = dir;
	
	//���� �ٶ󺸴� ���⿡�� �¿찡 x�ΰ� z�ΰ� �Ǻ�
	if (std::abs(dir.x) > std::abs(dir.z)) // x���� �� ũ�� �¿�
	{
		dir1.x += 0.5f; // ���������� �ణ �̵�
		dir2.x -= 0.5f; // �������� �ణ �̵�
	}
	else // z���� �� ũ�� �յ�
	{
		dir1.z += 0.5f; // ������ �ణ �̵�
		dir2.z -= 0.5f; // �ڷ� �ణ �̵�
	}

	pos.y = 0.5f; // ray cast height 

	std::vector<HitResult> hits;
	int size = RaycastAll(pos, dir, 2.f, ~0, hits);

	std::vector<HitResult> hits1;
	int size1 = RaycastAll(pos, dir1, 1.7f, ~0, hits1);
	std::vector<HitResult> hits2;
	int size2 = RaycastAll(pos, dir2, 1.7f, ~0, hits2);

	hits.insert(hits.end(), hits1.begin(), hits1.end());
	hits.insert(hits.end(), hits2.begin(), hits2.end());


	//std::cout << dir.x << " " << dir.y << " " << dir.z << std::endl;
	//std::cout << "Hit Count: " << size << std::endl;
	m_animator->SetParameter("Attack", true);
	/*GameObject* gumgiobj=nullptr;
	EffectComponent* gumgi = nullptr;
	auto childred = GetOwner()->m_childrenIndices;
	for (auto& child : childred)
	{
		gumgi = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
		if (gumgi)
		{
			gumgiobj = GameObject::FindIndex(child);
			break;
		}
	}*/

	/*Mathf::Vector3 pos2 = GetOwner()->m_transform.GetWorldPosition();
	auto forward2 = GetOwner()->m_transform.GetForward();
	auto offset{ 2 };
	auto offset2 = -forward2 * offset;
	pos2.x = offset2.x;
	pos2.y = 1;
	pos2.z = offset2.z;
	XMMATRIX lookAtMat = XMMatrixLookToRH(XMVectorZero(), forward2, XMVectorSet(0, 1, 0, 0));
	Quaternion swordRotation = Quaternion::CreateFromRotationMatrix(lookAtMat);
	if (gumgiobj)
	{
		gumgiobj->m_transform.SetPosition(pos2);
		gumgiobj->m_transform.SetRotation(swordRotation);
		gumgiobj->m_transform.UpdateWorldMatrix();
		if (gumgi)
		{
			gumgi->Apply();
		}

	}*/
	for (auto& hit : hits)
	{
		auto object = hit.hitObject;
		if (object == GetOwner()) continue;
		std::cout << object->m_name.data() << std::endl;

		//todo : �˾Ƽ� �ٲټ� player ���� Ȯ�� �ϰ� �������� �ֵ� �˾Ƽ��ϼ�
		Player* player = object->GetComponent<Player>();
		if (player)
		{
			player->SendDamage(this, 0);
		}
	}
	
	attackCount -= 1;
	blackBoard->SetValueAsInt("AttackCount", attackCount);
}


