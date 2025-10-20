#include "BootstrapObserver.h"
#include "SoundComponent.h"
#include "pch.h"

void BootstrapObserver::Start()
{
	auto obj = GameObject::Find("TitleSound");
	if (obj)
	{
		m_pSoundObject = obj->GetComponent<SoundComponent>();
	}

	m_pBootstrapObject = GameObject::Find("BootstrapCanvas");
}

void BootstrapObserver::Update(float tick)
{
	if (m_pBootstrapObject && !m_pBootstrapObject->IsEnabled())
	{
		if (m_pSoundObject && !m_pSoundObject->IsPlaying())
		{
			m_pSoundObject->Play();
		}
	}
}

