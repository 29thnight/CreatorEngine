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
			auto ptr = dynamic_cast<Component*>(this);
			auto sceneObject = ptr->GetOwner();
			if (ptr && sceneObject)
			{
				if (!ptr->IsEnabled() && sceneObject->IsDestroyMark())
				{
					return;
				}
				else if (!isStartCalled)
				{
					Awake();
					isStartCalled = true;
				}
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
	bool isStartCalled = false;
};