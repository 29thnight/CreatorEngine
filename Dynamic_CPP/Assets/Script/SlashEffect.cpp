#include "SlashEffect.h"
#include "pch.h"
#include "EffectComponent.h"
void SlashEffect::Start()
{
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
	 isstart = true;

}

void SlashEffect::Update(float tick)
{
	if (m_isCallStart == false)return;

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


	//����ִ� ����Ʈ ��������� �˾Ƽ� Ǯ�� ���Բ� 
	/*if (m_effect->m_isPlaying == false)
	{
		GetOwner()->Destroy();
	}*/
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

