#include "SpecialBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "PhysicsManager.h"
#include "Entity.h"
#include "Player.h"
#include "DebugLog.h"
#include "SphereColliderComponent.h"
void SpecialBullet::Start()
{
	__super::Start();
	bulletType = BulletType::Special;
}



void SpecialBullet::OnTriggerStay(const Collision& collision)
{
}

void SpecialBullet::Update(float tick)
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


void SpecialBullet::OnTriggerEnter(const Collision& collision)
{
	//피격된 지점 중심으로 범위 공격 
	if (hasAttacked == false)
	{
		//if (collision.otherObj->m_tag == "Enemy")
		{
			Entity* enemy = collision.otherObj->GetComponentDynamicCast<Entity>();
			if (enemy)
			{

				hasAttacked = true;

				Transform transform = GetOwner()->m_transform;

				std::vector<HitResult> hits;
				OverlapInput bulletInfo;
				
				bulletInfo.layerMask = 1 << 0 | 1 << 8 | 1 << 10;
				bulletInfo.position = transform.GetWorldPosition();
				bulletInfo.rotation = transform.GetWorldQuaternion();
				PhysicsManagers->SphereOverlap(bulletInfo, explosionRadius, hits);

				Mathf::Vector3 scale = collision.thisObj->m_transform.GetWorldScale();
				float radius = GetOwner()->GetComponent<SphereColliderComponent>()->GetRadius();
				explosionRadius = radius;
				float myRadius = explosionRadius;
				Mathf::Vector3 mypos = bulletInfo.position;
				


				for (auto& hit : hits)
				{
					auto object = hit.gameObject;
					Mathf::Vector3 otherpos = hit.gameObject->m_transform.GetWorldPosition();
					Mathf::Vector3 dir = otherpos - mypos;
					dir.Normalize();
					Mathf::Vector3 contactPoint = mypos + dir * myRadius;
					HitInfo hitinfo;
					hitinfo.hitPos = contactPoint;
					hitinfo.itemType = ItemType::Range;
					hitinfo.bulletType = BulletType::Special;
					if (object == GetOwner()) continue;

					auto enemy = object->GetComponentDynamicCast<Entity>();
					if (enemy)
					{
						enemy->SendDamage(m_ownerPlayer, m_damage, hitinfo);
					}
				}

				GetOwner()->Destroy(); //지우지말고 BulletPool만들기
			}
		}
	}
}