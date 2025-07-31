#include "EntityMineral.h"
#include "pch.h"
void EntityMineral::Start()
{
}

void EntityMineral::Update(float tick)
{
	if (m_currentHP <= 0)
	{
		GetOwner()->Destroy();
	}
}

void EntityMineral::OnDestroy()
{

	//부서지면서 EntityItem 달린 오브젝트 드랍
}

