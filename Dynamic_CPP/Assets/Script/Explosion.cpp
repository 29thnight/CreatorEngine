#include "Explosion.h"
#include "pch.h"
#include "EffectComponent.h"
void Explosion::Start()
{
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
}

void Explosion::Update(float tick)
{

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
	if (nullptr == m_effect)
	{
		m_effect = GetOwner()->GetComponent<EffectComponent>();
	}
	if (m_effect)
	{
		m_effect->Awake(); //awake ��ġ�� ����̾ȵ� 
		m_effect->Apply();
	}
}

