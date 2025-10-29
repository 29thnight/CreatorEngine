#include "SwordProjectile.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "EffectComponent.h"
#include "GameManager.h"
#include "Entity.h"
#include "ObjectPoolManager.h"
#include "BoxColliderComponent.h"
#include "Camera.h"
void SwordProjectile::Start()
{
	if (m_effects.empty())
	{
		auto effect = GetOwner()->GetComponent<EffectComponent>();
		if (effect)
		{
			m_effects.push_back(effect);
		}
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent);
			}
		}
	}
}

void SwordProjectile::OnTriggerEnter(const Collision& collision)
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
	hitinfo.hitNormal = -GetOwner()->m_transform.GetForward();
	if (enemy)
	{
		auto [iter, inserted] = targets.insert(enemy);
		if (inserted) (*iter)->SendDamage(m_ownerPlayer, m_damage, hitinfo);
	}
}



void SwordProjectile::Update(float tick)
{
	if (m_isCallStart == false)return;
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		for (auto& effect : m_effects)
		{
			effect->Apply();
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
				for (auto& effect : m_effects)
				{
					effect->StopEffect();
				}
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
				GM->GetObjectPoolManager()->GetSwordProjectile()->Push(this->GetOwner());
				for (auto& effect : m_effects)
				{
					effect->StopEffect();
				}
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
				GM->GetObjectPoolManager()->GetSwordProjectile()->Push(this->GetOwner());
				for (auto& effect : m_effects)
				{
					effect->StopEffect();
				}
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

	if (m_effects.empty())
	{
		auto effect = GetOwner()->GetComponent<EffectComponent>();
		if (effect)
		{
			m_effects.push_back(effect);
		}
		auto childred = GetOwner()->m_childrenIndices;
		for (auto& child : childred)
		{
			auto effectcomponent = GameObject::FindIndex(child)->GetComponent<EffectComponent>();
			if (effectcomponent)
			{
				m_effects.push_back(effectcomponent);
			}
		}
	}
	for (auto& effect : m_effects)
	{
		effect->Apply();
	}
}

