#include "Bullet.h"
#include "pch.h"
#include "Player.h"
#include "GameObject.h"
void Bullet::Start()
{
}

void Bullet::Update(float tick)
{
}

void Bullet::Initialize(Player* owner,Mathf::Vector3 originpos,Mathf::Vector3 dir, int _damage)
{
	Transform* transform = GetOwner()->GetComponent<Transform>();
	transform->SetPosition(originpos);
	m_owenrPlayer = owner;
	m_moveDir = dir;
	m_damage = _damage;
	hasAttacked = false;
	lifeTime = 5.f;
}

