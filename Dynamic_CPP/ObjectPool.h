#pragma once
#include "Core.Minimal.h"

class GameObject;
class ObjectPool
{

	std::string prefabName;
	std::vector<GameObject*> pool;
public:
	

	void InitializePool(std::string _prefabName,int size); //�������̸��� ���鰹��

	//���� ��ũ��Ʈ �ʱ�ȭ���� Ǫ�� ���� �˾Ƽ� �������
	bool Empty();
	void Push(GameObject* gmaObj);
	GameObject* Pop();
	void ClearPool();
};