#include "Bullet.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
#include "EffectComponent.h"
void Bullet::Start()
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

void Bullet::Update(float tick)
{
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		if (m_effect)
		{
			m_effect->Awake(); //awake 안치면 출력이안됨 
			m_effect->Apply();
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}
}

void Bullet::Initialize(Player* owner,Mathf::Vector3 originpos,Mathf::Vector3 dir, int _damage)
{
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->SetPosition(originpos);
	m_ownerPlayer = owner;
	m_moveDir = dir;
	m_damage = _damage;
	hasAttacked = false;
	lifeTime = 5.f;

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

