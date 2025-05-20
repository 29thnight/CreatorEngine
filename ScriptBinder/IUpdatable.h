#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
#include "Scene.h"

interface IUpdatable
{
    IUpdatable()
    {
        subscribedScene = SceneManagers->GetActiveScene();
        if (!subscribedScene)
        {
            return;
        }

        m_updateEventHandle = subscribedScene->UpdateEvent.AddLambda([this](float deltaSecond)
        {
            auto ptr = dynamic_cast<Component*>(this);
			auto sceneObject = ptr->GetOwner();
			if (nullptr != ptr && sceneObject)
			{
                if (!ptr->IsEnabled() && sceneObject->IsDestroyMark())
                {
                    return;
                }
                else
                {
                    Update(deltaSecond);
                }
			}
        });
    }
    virtual ~IUpdatable()
    {
        subscribedScene->UpdateEvent.Remove(m_updateEventHandle);
    }

    virtual void Update(float deltaSecond) = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_updateEventHandle{};
};
