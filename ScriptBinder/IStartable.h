#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

interface IStartable
{
	IStartable()
	{
		subscribedScene = SceneManagers->GetActiveScene();
		if (!subscribedScene)
		{
			return;
		}

		m_startEventHandle = subscribedScene->StartEvent.AddLambda([this]
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
					Start();
					isStartCalled = true;
				}
			}
		});
	}

	virtual ~IStartable()
	{
		subscribedScene->StartEvent.Remove(m_startEventHandle);
	}

	virtual void Start() = 0;

protected:
	Scene* subscribedScene{};
	Core::DelegateHandle m_startEventHandle{};
	bool isStartCalled = false;
};