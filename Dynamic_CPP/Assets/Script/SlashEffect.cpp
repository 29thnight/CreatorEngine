#include "SlashEffect.h"
#include "pch.h"
#include "EffectComponent.h"
void SlashEffect::Start()
{
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
}

void SlashEffect::Update(float tick)
{
	if (true == beLateFrame && false == OnEffect)
	{
		OnEffect = true;
		if (m_effect)
		{
			m_effect->Awake(); //awake 안치면 출력이안됨 
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

void SlashEffect::Initialize()
{
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
	beLateFrame = false;
	OnEffect = false;
}

