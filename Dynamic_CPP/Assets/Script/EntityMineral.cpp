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

	//�μ����鼭 EntityItem �޸� ������Ʈ ���
}

