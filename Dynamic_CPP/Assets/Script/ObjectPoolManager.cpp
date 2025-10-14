#include "ObjectPoolManager.h"
#include "pch.h"
#include "GameManager.h"
void ObjectPoolManager::Start()
{
	auto gameManager = GameObject::Find("GameManager");
	if (gameManager)
	{
		auto gm = gameManager->GetComponent<GameManager>();
		if (gm)
		{
			gm->PushObjectPoolManager(this);
		}
	}


	normalBulletPool.InitializePool("BulletNormal", 50);
	specialBulletPool.InitializePool("BulletSpecial",20);
	bombPool.InitializePool("Bomb", 20);


}

void ObjectPoolManager::Update(float tick)
{
}

