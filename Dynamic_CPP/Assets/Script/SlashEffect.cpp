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
			m_effect->Awake(); //awake ��ġ�� ����̾ȵ� 
			m_effect->Apply();
		}
	}


	if (false == beLateFrame)
	{
		beLateFrame = true;
	}


	//����ִ� ����Ʈ ��������� �˾Ƽ� Ǯ�� ���Բ� 
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

