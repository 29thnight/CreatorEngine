#include "SwordProjectile.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "EffectComponent.h"
#include "GameManager.h"
#include "Entity.h"
#include "ObjectPoolManager.h"
#include "BoxColliderComponent.h"
void SwordProjectile::Start()
{
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

void SwordProjectile::OnCollisionEnter(const Collision& collision)
{
	auto collider = GetComponent<BoxColliderComponent>();
	Mathf::Vector3 boxExtent = collider->GetBoxInfo().boxExtent;
	Entity* enemy = collision.otherObj->GetComponentDynamicCast<Entity>();

	Mathf::Vector3 mypos = GetOwner()->m_transform.GetWorldPosition();
	Mathf::Vector3 otherpos = collision.otherObj->m_transform.GetWorldPosition();
	Mathf::Vector3 dir = otherpos - mypos;
	dir.Normalize();
	Mathf::Vector3 contactPoint = mypos + dir * boxExtent;
	HitInfo hitinfo;
	hitinfo.hitPos = contactPoint;
	hitinfo.itemType = ItemType::Melee;
	if (enemy)
	{
		auto [iter, inserted] = targets.insert(enemy);
		if (inserted) (*iter)->SendDamage(m_ownerPlayer, m_damage, hitinfo);
	}
}

void SwordProjectile::Update(float tick)
{
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		if (m_effect)
		{
			m_effect->Apply();
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}
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
				GM->GetObjectPoolManager()->GetSwordProjectile()->Push(this->GetOwner());
				m_effect->StopEffect();
			}

		}
		lifeTime = rangedProjDist;
	}

}

void SwordProjectile::Initialize(Player* owner, Mathf::Vector3 originpos, Mathf::Vector3 dir, int _damage)
{
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->SetPosition(originpos);
	m_ownerPlayer = owner;
	m_moveDir = dir;
	m_damage = _damage;
	lifeTime = rangedProjDist;
	beLateFrame = false;
	OnEffect = false;
	targets.clear();
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

