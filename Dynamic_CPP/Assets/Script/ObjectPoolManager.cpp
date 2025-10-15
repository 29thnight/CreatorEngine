#include "ObjectPoolManager.h"
#include "pch.h"
#include "GameManager.h"
void ObjectPoolManager::Start()
{
	auto gm = GetOwner()->GetComponent<GameManager>();
	if (gm)
	{
		gm->PushObjectPoolManager(this);
		normalBulletPool.InitializePool("BulletNormal", 30);
		specialBulletPool.InitializePool("BulletSpecial", 10);
		bombPool.InitializePool("Bomb", 15);
		swordProjectilePool.InitializePool("SwordProjectile", 5);
	}


}

void ObjectPoolManager::Update(float tick)
{
}

