#include "SpecialBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "PhysicsManager.h"
#include "Entity.h"
#include "Player.h"
#include "DebugLog.h"
#include "SphereColliderComponent.h"
#include "GameManager.h"
#include "ObjectPoolManager.h"
#include "EffectComponent.h"
#include "PrefabUtility.h"
void SpecialBullet::Start()
{
	__super::Start();
	bulletType = BulletType::Special;
	lifeTime = rangedProjDist;
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
		auto GMobj = GameObject::Find("GameManager");
		if (GMobj)
		{
			auto GM = GMobj->GetComponent<GameManager>();
			if (GM && GM->GetObjectPoolManager() != nullptr)
			{
				GM->GetObjectPoolManager()->GetSpecialBulletPool()->Push(this->GetOwner());
				m_effect->StopEffect();
			}
		}
		lifeTime = rangedProjDist;
	}
}

void SpecialBullet::Initialize(Player* owner, Mathf::Vector3 originpos, Mathf::Vector3 dir, int _damage)
{
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->SetPosition(originpos);
	m_ownerPlayer = owner;
	m_moveDir = dir;
	m_damage = _damage;
	hasAttacked = false;
	lifeTime = rangedProjDist;
	beLateFrame = false;
	OnEffect = false;

	if (nullptr == m_effect)
	{
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{

			m_effect = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			break;
		}
	}
}


void SpecialBullet::OnTriggerEnter(const Collision& collision)
{
	//피격된 지점 중심으로 범위 공격 
	if (hasAttacked == false)
	{
		//if (collision.otherObj->m_tag == "Enemy")
		{
			//Entity* enemy = collision.otherObj->GetComponentDynamicCast<Entity>();
			//if (enemy)
			{

				hasAttacked = true;

				Transform transform = GetOwner()->m_transform;

				std::vector<HitResult> hits;
				OverlapInput bulletInfo;
				
				bulletInfo.layerMask = 1 << 0 | 1 << 8 | 1 << 10  | 1<< 15;
				bulletInfo.position = transform.GetWorldPosition();
				bulletInfo.rotation = transform.GetWorldQuaternion();
				PhysicsManagers->SphereOverlap(bulletInfo, explosionRadius, hits);

				Mathf::Vector3 scale = collision.thisObj->m_transform.GetWorldScale();
				float radius = GetOwner()->GetComponent<SphereColliderComponent>()->GetRadius();
				float myRadius = explosionRadius;
				Mathf::Vector3 mypos = bulletInfo.position;
				
				//test
				Prefab* test = PrefabUtilitys->LoadPrefab("TestSphere");
				if (test)
				{
					GameObject* testObj = PrefabUtilitys->InstantiatePrefab(test, "test");
					testObj->GetComponent<Transform>()->SetPosition(mypos);
					testObj->GetComponent<SphereColliderComponent>()->SetRadius(myRadius);
				}

				for (auto& hit : hits)
				{
					auto object = hit.gameObject;
					Mathf::Vector3 otherpos = hit.gameObject->m_transform.GetWorldPosition();
					Mathf::Vector3 dir = otherpos - mypos;
					dir.Normalize();
					Mathf::Vector3 contactPoint = mypos + dir * myRadius;
					HitInfo hitinfo;
					//hitinfo.hitPos = contactPoint;
					otherpos.y += 1.f;
					hitinfo.hitPos = otherpos;
					hitinfo.itemType = ItemType::Range;
					hitinfo.bulletType = BulletType::Special;
					if (object == GetOwner()) continue;

					auto enemy = object->GetComponentDynamicCast<Entity>();
					if (enemy)
					{
						enemy->SendDamage(m_ownerPlayer, m_damage, hitinfo);
					}
				}

				auto GMobj = GameObject::Find("GameManager");
				if (GMobj)
				{
					auto GM = GMobj->GetComponent<GameManager>();
					if (GM && GM->GetObjectPoolManager() != nullptr)
					{
						GM->GetObjectPoolManager()->GetSpecialBulletPool()->Push(this->GetOwner());
						m_effect->StopEffect();
					}

				}
			}
		}
	}
}