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
	//		auto forward = GetOwner()->m_transform.GetForward(); //맞은 방향에서 밀리게끔 수정
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
		//이펙트 위치를 오브젝트의 회전과 상관없이 카메라쪽에서 보이게끔 옮기기
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
		return; // 공격 방향이 없으면 리턴
	}
	dir = blackBoard->GetValueAsVector3("AttackDirection");

	//dir.z = -dir.z; // z축 반전, z축이 앞으로 가도록
	dir.y = 0.f; // y축은 무시하고 수평 방향으로만 공격
	//transform forward base 나중에 쓰자
	//Mathf::Vector3 forward = GetOwner()->m_transform.GetForward();
	//pos += forward * 2.f;
	//케릭터 높이 고려 기본 높이 0.5로 판단

	//궤적에 따른 레이방향 2개 추가
	Mathf::Vector3 dir1 = dir;
	Mathf::Vector3 dir2 = dir;
	
	//지금 바라보는 방향에서 좌우가 x인가 z인가 판별
	if (std::abs(dir.x) > std::abs(dir.z)) // x축이 더 크면 좌우
	{
		dir1.x += 0.5f; // 오른쪽으로 약간 이동
		dir2.x -= 0.5f; // 왼쪽으로 약간 이동
	}
	else // z축이 더 크면 앞뒤
	{
		dir1.z += 0.5f; // 앞으로 약간 이동
		dir2.z -= 0.5f; // 뒤로 약간 이동
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

		//todo : 알아서 바꾸셈 player 인지 확인 하고 데미지를 주든 알아서하셈
		Player* player = object->GetComponent<Player>();
		if (player)
		{
			player->SendDamage(this, 0);
		}
	}
	
	attackCount -= 1;
	blackBoard->SetValueAsInt("AttackCount", attackCount);
}


