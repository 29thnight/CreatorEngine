#include "Explosion.h"
#include "pch.h"
void Explosion::Start()
{
}

void Explosion::Update(float tick)
{
	if (endAttack == false) //한번 SphereOverlap으로 공격후 끝  or 터진곳 지속 얘가떄리기 or 폭탄투사체가 도착하면 때리기 
	{

		endAttack = true;
	}
}

void Explosion::Init(Player* _owner)
{
	m_ownerPlayer = m_ownerPlayer;
}

