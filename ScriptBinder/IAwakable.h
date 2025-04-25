#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

interface IAwakable
{
	IAwakable()
	{
		Scene* scene = SceneManagers->GetActiveScene();
		if (!scene)
		{
			return;
		}

		m_awakeEventHandle = scene->AwakeEvent.AddLambda([this]
		{
			Awake();
			SceneManagers->GetActiveScene()->AwakeEvent.Remove(m_awakeEventHandle);
		});
	}

	virtual ~IAwakable()
	{
	}

	virtual void Awake() = 0;

protected:
	Core::DelegateHandle m_awakeEventHandle{};
};