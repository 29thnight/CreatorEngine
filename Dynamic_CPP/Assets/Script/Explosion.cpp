#include "Explosion.h"
#include "pch.h"
void Explosion::Start()
{
}

void Explosion::Update(float tick)
{
	if (endAttack == false) //�ѹ� SphereOverlap���� ������ ��  or ������ ���� �갡������ or ��ź����ü�� �����ϸ� ������ 
	{

		endAttack = true;
	}
}

void Explosion::Init(Player* _owner)
{
	m_ownerPlayer = m_ownerPlayer;
}

