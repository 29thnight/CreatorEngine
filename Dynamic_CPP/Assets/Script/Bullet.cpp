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

void Bullet::Initialize(Player* owner, Mathf::Vector3 dir, int _damage)
{
	m_owenrPlayer = owner;
	m_moveDir = dir;
	m_damage = _damage;
	hasFired = false;
	lifeTime = 5.f;
}

