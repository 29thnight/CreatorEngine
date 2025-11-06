#include "NormalBullet.h"
#include "pch.h"
#include "EntityEnemy.h"
#include "Player.h"
#include "DebugLog.h"
#include "Entity.h"
#include "SphereColliderComponent.h"
#include "ObjectPoolManager.h"
#include "GameManager.h"
#include "EffectComponent.h"
#include "CameraComponent.h"
void NormalBullet::Start()
{
	__super::Start();
	bulletType = BulletType::Normal;
}


void NormalBullet::Update(float tick)
{
	__super::Update(tick);
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->AddPosition(m_moveDir * rangedProjSpd * tick);

	lifeTime -= tick;
	if (lifeTime <= 0)
	{
		auto GMobj = GameObject::Find("GameManager");
		if (GMobj)
		{
			auto GM = GMobj->GetComponent<GameManager>();
			if (GM && GM->GetObjectPoolManager() != nullptr)
			{
				GM->GetObjectPoolManager()->GetNormalBulletPool()->Push(this->GetOwner());
				m_effect->StopEffect();
			}

		}
		lifeTime = rangedProjDist;
	}


	auto camera = CameraManagement->GetLastCamera().get();

	auto camViewProj = camera->CalculateView() * camera->CalculateProjection();
	auto invCamViewProj = XMMatrixInverse(nullptr, camViewProj);

	XMVECTOR worldpos = GetOwner()->m_transform.GetWorldPosition();
	XMVECTOR clipSpacePos = XMVector3TransformCoord(worldpos, camViewProj);
	float w = XMVectorGetW(clipSpacePos);
	if (w < 0.001f) {
		//카메라 뒤편
		//여기로 오면 뭔가 잘못 된거임
		auto GMobj = GameObject::Find("GameManager");
		if (GMobj)
		{
			auto GM = GMobj->GetComponent<GameManager>();
			if (GM && GM->GetObjectPoolManager() != nullptr)
			{
				GM->GetObjectPoolManager()->GetNormalBulletPool()->Push(this->GetOwner());
				m_effect->StopEffect();
			}

		}
		lifeTime = rangedProjDist;
	}

	XMVECTOR ndcPos = XMVectorScale(clipSpacePos, 1.0f / w);

	float x = XMVectorGetX(ndcPos);
	float y = XMVectorGetY(ndcPos);
	x = abs(x);
	y = abs(y);


	if (x > 1 || y > 1)
	{
		//카메라 x축 혹은 y축 밖
		//여기는 일절 충돌 없이 카메라 밖으로 나간경우
		auto GMobj = GameObject::Find("GameManager");
		if (GMobj)
		{
			auto GM = GMobj->GetComponent<GameManager>();
			if (GM && GM->GetObjectPoolManager() != nullptr)
			{
				GM->GetObjectPoolManager()->GetNormalBulletPool()->Push(this->GetOwner());
				m_effect->StopEffect();
			}

		}
		lifeTime = rangedProjDist;
	}
}

void NormalBullet::Initialize(Player* owner, Mathf::Vector3 originpos, Mathf::Vector3 dir, int _damage)
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
	

			}
			hasAttacked = true;

			auto GMobj = GameObject::Find("GameManager");
			if (GMobj)
			{
				auto GM = GMobj->GetComponent<GameManager>();
				if (GM && GM->GetObjectPoolManager() != nullptr)
				{
					GM->GetObjectPoolManager()->GetNormalBulletPool()->Push(this->GetOwner());
					m_effect->StopEffect();
				}

			}
		}
	}
}
