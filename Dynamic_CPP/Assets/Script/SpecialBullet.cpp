#include "SpecialBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "PhysicsManager.h"
#include "Entity.h"
#include "Player.h"
void SpecialBullet::Start()
{
	bulletType = BulletType::Special;
}



void SpecialBullet::OnTriggerStay(const Collision& collision)
{
}

void SpecialBullet::Update(float tick)
{
}


void SpecialBullet::OnTriggerEnter(const Collision& collision)
{
	//피격된 지점 중심으로 범위 공격 //새 오브젝트 만들어서 걔가 처리
	if (hasAttacked == false)
	{
		if (collision.otherObj->m_tag == "Enemy")
		{
			EntityEnemy* enemy = collision.otherObj->GetComponent<EntityEnemy>();

			std::cout << "EnemyHit!" << std::endl;

			hasAttacked = true;

			Transform transform = GetOwner()->m_transform;

			std::vector<HitResult> hits;
			OverlapInput bulletInfo;
			bulletInfo.layerMask = 1u; //일단 다떄림
			bulletInfo.position = transform.GetWorldPosition();
			bulletInfo.rotation = transform.GetWorldQuaternion();
			PhysicsManagers->SphereOverlap(bulletInfo, explosionRadius, hits);


			for (auto& hit : hits)
			{
				auto object = hit.gameObject;
				if (object == GetOwner()) continue;

				auto enemy = object->GetComponent<EntityEnemy>();
				if (enemy)
				{
					enemy->SendDamage(m_owenrPlayer, 1);
				}
			}

			GetOwner()->Destroy(); //지우지말고 BulletPool만들기
		}
	}
}