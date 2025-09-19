#include "Explosion.h"
#include "pch.h"
void Explosion::Start()
{
}

void Explosion::Update(float tick)
{

	//들고있는 이펙트 재생끝나면 알아서 풀로 들어가게끔 
	if (endAttack == false) 
	{

		endAttack = true;
	}
}

void Explosion::Init(Player* _owner)
{
	m_ownerPlayer = m_ownerPlayer;
}

