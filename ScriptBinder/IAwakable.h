#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

interface IAwakable
{
	IAwakable()
	{
		subscribedScene = SceneManagers->GetActiveScene();
		if (!subscribedScene)
		{
			return;
		}

		m_awakeEventHandle = subscribedScene->AwakeEvent.AddLambda([this]
		{
			static bool isAwakeCalled = false;
			if (false == isAwakeCalled)
			{
				Awake();
				isAwakeCalled = true;
			}
		});
	}

	virtual ~IAwakable()
	{
		subscribedScene->AwakeEvent.Remove(m_awakeEventHandle);
	}

	virtual void Awake() = 0;

protected:
	Scene* subscribedScene{};
	Core::DelegateHandle m_awakeEventHandle{};
};