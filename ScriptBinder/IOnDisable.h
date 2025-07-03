#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

interface IOnDisable
{
	IOnDisable()
	{
		subscribedScene = SceneManagers->GetActiveScene();
		if (!subscribedScene)
		{
			return;
		}

		m_onDisableEventHandle = subscribedScene->OnDisableEvent.AddLambda([this]
		{
			auto ptr = dynamic_cast<Component*>(this);
			auto sceneObject = ptr->GetOwner();
			if (ptr && sceneObject)
			{
				bool isDisabled = !ptr->IsEnabled();
				if(isDisabled != prevDisableState)
				{
					if (true == isDisabled)
					{
						OnDisable();
					}
					prevDisableState = isDisabled;
				}
			}
		});
	}

	virtual ~IOnDisable()
	{
		subscribedScene->OnDisableEvent.Remove(m_onDisableEventHandle);
	}

	virtual void OnDisable() = 0;

protected:
	Scene* subscribedScene{};
	Core::DelegateHandle m_onDisableEventHandle{};
	bool prevDisableState = false;
};