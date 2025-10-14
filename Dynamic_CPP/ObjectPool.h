#pragma once
#include "Core.Minimal.h"

class GameObject;
class ObjectPool
{

	std::string prefabName;
	std::vector<GameObject*> pool;
public:
	

	void InitializePool(std::string _prefabName,int size); //프리펩이름과 만들갯수

	//개별 스크립트 초기화등은 푸쉬 팝후 알아서 해줘야함
	bool Empty();
	void Push(GameObject* gmaObj);
	GameObject* Pop();
	void ClearPool();
};