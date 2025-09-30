#include "Explosion.h"
#include "pch.h"
#include "EffectComponent.h"
#include "Player.h"
void Explosion::Start()
{
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
}

void Explosion::Update(float tick)
{
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		if (m_effect)
		{
			m_effect->Apply();
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}

	//들고있는 이펙트 재생끝나면 알아서 풀로 들어가게끔 
	if (m_effect->m_isPlaying == false)
	{
		GetOwner()->Destroy();
	}
}

void Explosion::Initialize(Player* _owner)
{
	m_ownerPlayer = m_ownerPlayer;
	endAttack = false;
	OnEffect = false;
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
	if (m_effect)
	{
		//onEffect = true;
		//m_effect->Awake(); //awake 안치면 출력이안됨 
		//m_effect->Apply();

	}
}

