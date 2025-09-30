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

	//����ִ� ����Ʈ ��������� �˾Ƽ� Ǯ�� ���Բ� 
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
		//m_effect->Awake(); //awake ��ġ�� ����̾ȵ� 
		//m_effect->Apply();

	}
}

