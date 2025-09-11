#include "EntityEnemy.h"
#include "pch.h"
#include "Player.h"
#include "EffectComponent.h"
#include "BehaviorTreeComponent.h"
#include "Blackboard.h"
#include "RaycastHelper.h"
#include "Animator.h"
#include "CharacterControllerComponent.h"
#include "DebugLog.h"
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
}

void EntityEnemy::OnTriggerEnter(const Collision& collision)
{
	bool hasAsis = blackBoard->HasKey("Asis");
	GameObject* asis = nullptr;
	std::string asisname = "";
	if (hasAsis) {
		asis = blackBoard->GetValueAsGameObject("Asis");
		asisname = asis->GetHashedName().ToString();
	}
	std::string colname = collision.otherObj->GetHashedName().ToString();

	if (colname == asisname)
	{
		SimpleMath::Vector3 p0 = collision.contactPoints[0]; //충돌지점 
		Transform* m_transform = GetOwner()->GetComponent<Transform>();
		SimpleMath::Vector3 p1 = m_transform->GetWorldPosition(); //이 오브젝트 위치
		//asis가 부딫히면 밀려나는 방향
		SimpleMath::Vector3 invDir = p1 - p0;

		//현제 위치에서 밀려나는 방향으로 이동된 세로운 위치
		invDir.y = 0.f;
		invDir.Normalize();

		SimpleMath::Vector3 newPos = p1 + invDir * 0.5f; //현제는 0.5f만 밀려나게 설정 
		//더 깔끔하게 혹은 겹쳐지는 거리 판단 방법 필요
		//부딫히면 반대 방향으로 자연스럽게 밀려나도록 케릭터 컨트롤러에 설정
		auto controllerComp = m_pOwner->GetComponent<CharacterControllerComponent>();
		if (controllerComp)
		{
			UINT cctID = controllerComp->GetControllerInfo().id;
			if (cctID != 0)
			{
				//CCT로 밀려나는 위치로 이동 추가적으로 GameObject 위치도 변경
				m_transform->SetPosition(newPos);
				PhysicsManagers->SetControllerPosition(cctID, newPos);
				std::cout << m_pOwner->GetHashedName().ToString() << " Asis OnTriggerEnter CCT Move" << std::endl;
			}
		}
	}
}

void EntityEnemy::OnCollisionEnter(const Collision& collision)
{
	bool hasAsis = blackBoard->HasKey("Asis");
	GameObject* asis = nullptr;
	std::string asisname = "";
	if (hasAsis) {
		asis = blackBoard->GetValueAsGameObject("Asis");
		asisname = asis->GetHashedName().ToString();
	}
	std::string colname = collision.otherObj->GetHashedName().ToString();

	if (colname == asisname)
	{
	   SimpleMath::Vector3 p0 = collision.contactPoints[0]; //충돌지점 
	   Transform* m_transform = GetOwner()->GetComponent<Transform>();
	   SimpleMath::Vector3 p1 = m_transform->GetWorldPosition(); //이 오브젝트 위치
	   //asis가 부딫히면 밀려나는 방향
	   SimpleMath::Vector3 invDir =p1 - p0;

		//현제 위치에서 밀려나는 방향으로 이동된 세로운 위치
	   invDir.y = 0.f;
	   invDir.Normalize();

	   SimpleMath::Vector3 newPos = p1 + invDir * 0.5f; //현제는 0.5f만 밀려나게 설정 
		//더 깔끔하게 혹은 겹쳐지는 거리 판단 방법 필요
		//부딫히면 반대 방향으로 자연스럽게 밀려나도록 케릭터 컨트롤러에 설정
	   auto controllerComp = m_pOwner->GetComponent<CharacterControllerComponent>();
	   if (controllerComp)
	   {
		   UINT cctID = controllerComp->GetControllerInfo().id;
		   if (cctID != 0)
		   {
			   //CCT로 밀려나는 위치로 이동 추가적으로 GameObject 위치도 변경
			   m_transform->SetPosition(newPos);
			   PhysicsManagers->SetControllerPosition(cctID, newPos);
			   std::cout << m_pOwner->GetHashedName().ToString() << " Asis onCollisionEnter CCT Move" << std::endl;
		   }
	   }
	}
}

void EntityEnemy::Update(float tick)
{
	Mathf::Vector3 forward = enemy->m_transform.GetForward();
	//LOG("Enemy Forward: " << forward.x << " " << forward.y << " " << forward.z);

	attackCount = blackBoard->GetValueAsInt("AttackCount");

	if (hittimer > 0.f) {
		hittimer -= tick;
		if (hittimer < 0.f) hittimer = 0.f;
		m_animator->GetOwner()->m_transform.SetPosition(Mathf::Vector3::Lerp(Mathf::Vector3::Zero, hitPos, hittimer / m_MaxknockBackTime));
		m_animator->GetOwner()->m_transform.SetScale(Mathf::Vector3::Lerp(hitBaseScale, hitBaseScale * m_knockBackScaleVelocity, hittimer / m_MaxknockBackTime));
	}
	if (attackCount > 0) {
		m_animator->SetParameter("Attack", true);
		attackCount = 0;
		blackBoard->SetValueAsInt("AttackCount", attackCount);
	}

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
			Mathf::Vector3 curPos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 senderPos = sender->GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = curPos - senderPos;

			dir.Normalize();
			Mathf::Vector3 p = XMVector3Rotate(dir * m_knockBackVelocity, XMQuaternionInverse(m_animator->GetOwner()->m_transform.GetWorldQuaternion()));
			hittimer = m_MaxknockBackTime;
			hitPos = p;
			m_animator->GetOwner()->m_transform.SetScale(hitBaseScale * m_knockBackScaleVelocity);

			
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


	//LOG(dir.x << " " << dir.y << " " << dir.z);
	//LOG("Hit Count: " << size);
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
		auto object = hit.gameObject;
		if (object == GetOwner()) continue;
		LOG(object->m_name.data());

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


