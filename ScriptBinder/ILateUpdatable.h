#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
#include "Scene.h"

interface ILateUpdatable
{
    ILateUpdatable()
    {
        subscribedScene = SceneManagers->GetActiveScene();
        if (!subscribedScene)
        {
            return;
        }

        m_lateUpdateEventHandle = subscribedScene->LateUpdateEvent.AddLambda([this](float deltaSecond)
        {
            GameObject* sceneObject{};

            auto ptr = dynamic_cast<Component*>(this);
            if (nullptr != ptr)
            {
                sceneObject = ptr->GetOwner();
            }

            if (nullptr != ptr && sceneObject)
            {
                if (!ptr->IsEnabled() && sceneObject->IsDestroyMark())
                {
                    return;
                }
                else
                {
                    LateUpdate(deltaSecond);
                }
            }
        });
    }
    virtual ~ILateUpdatable()
    {
        subscribedScene->LateUpdateEvent -= m_lateUpdateEventHandle;
    }

    virtual void LateUpdate(float deltaSecond) = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_lateUpdateEventHandle{};
};
