#include "ObjectPool.h"
#include "PrefabUtility.h"
void ObjectPool::InitializePool(std::string _prefabName, int size)
{
	prefabName = _prefabName;
	Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName);
	if (prefab)
	{
		for(int i=0;i<size;i++)
		{
			GameObject* gameObj = PrefabUtilitys->InstantiatePrefab(prefab, "GameObject");
			Push(gameObj);
		}
	}
}

void ObjectPool::Push(GameObject* gameObj)
{
	if (!gameObj)
		return;

	// 이미 풀 안에 있는지 체크 (간단히)
	if (std::find(pool.begin(), pool.end(), gameObj) != pool.end())
		return;
	gameObj->SetEnabled(false);
	pool.push_back(gameObj);

}

GameObject* ObjectPool::Pop()
{
	if(pool.empty())
	{
		Prefab* prefab = PrefabUtilitys->LoadPrefab(prefabName);
		for(int i =0;i<10; i++) 
		{
			if (prefab)
			{
				GameObject* gameObj = PrefabUtilitys->InstantiatePrefab(prefab, "GameObject");
				Push(gameObj);
			}
		}
	}
	GameObject* obj = pool.back();
	pool.pop_back();
	obj->SetEnabled(true);
	return obj;
}



void ObjectPool::ClearPool()
{
	for (auto obj : pool)
	{
		if (obj)
		{
			obj->Destroy();
		}
	}
	pool.clear();
}
