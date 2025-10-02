#include "NormalBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "Player.h"
#include "DebugLog.h"
#include "Entity.h"
#include "SphereColliderComponent.h"

void NormalBullet::Start()
{
	__super::Start();
	bulletType = BulletType::Normal;
}


void NormalBullet::Update(float tick)
{
	__super::Update(tick);
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->AddPosition(m_moveDir * rangedProjSpd);

	lifeTime -= tick;
	if (lifeTime <= 0)
	{
		GetOwner()->Destroy();
	}
}


void NormalBullet::OnTriggerEnter(const Collision& collision)
{

	auto collider = GetComponent<SphereColliderComponent>();
	float myRadius = collider->GetRadius();
	//하나만 떄리고 삭제 
	if (hasAttacked == false)
	{
		//if (collision.otherObj->m_tag == "Enemy")
		{
			Entity* enemy = collision.otherObj->GetComponentDynamicCast<Entity>();
			
			Mathf::Vector3 mypos = GetOwner()->m_transform.GetWorldPosition();
			Mathf::Vector3 otherpos = collision.otherObj->m_transform.GetWorldPosition();
			Mathf::Vector3 dir = otherpos - mypos;
			dir.Normalize();
			Mathf::Vector3 contactPoint = mypos + dir * myRadius;
			HitInfo hitinfo;
			hitinfo.hitPos = contactPoint;
			hitinfo.itemType = ItemType::Range;
			hitinfo.bulletType = bulletType;
			if (enemy)
			{
				LOG("EnemyHit!");
				enemy->SendDamage(m_ownerPlayer, m_damage, hitinfo);
				hasAttacked = true;
				GetOwner()->Destroy(); //지우지말고 BulletPool만들기

			}
		}
	}
}
