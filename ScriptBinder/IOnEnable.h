#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

interface IOnEnable
{
	IOnEnable()
	{
		subscribedScene = SceneManagers->GetActiveScene();
		if (!subscribedScene)
		{
			return;
		}

		m_onEnableEventHandle = subscribedScene->OnDisableEvent.AddLambda([this]
		{
			auto ptr = dynamic_cast<Component*>(this);
			auto sceneObject = ptr->GetOwner();
			if (ptr && sceneObject)
			{
				bool isEisabled = ptr->IsEnabled();
				if (isEisabled != prevEnableState)
				{
					if (true == isEisabled)
					{
						OnEnable();
					}
					prevEnableState = isEisabled;
				}
			}
		});
	}

	virtual ~IOnEnable()
	{
		subscribedScene->OnDisableEvent.Remove(m_onEnableEventHandle);
	}

	virtual void OnEnable() = 0;

protected:
	Scene* subscribedScene{};
	Core::DelegateHandle m_onEnableEventHandle{};
	bool prevEnableState = false;
};