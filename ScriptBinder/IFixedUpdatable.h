#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
#include "Scene.h"

interface IFixedUpdatable
{
    IFixedUpdatable()
    {
        subscribedScene = SceneManagers->GetActiveScene();
        if (!subscribedScene)
        {
            return;
        }

        m_fixedUpdateEventHandle = subscribedScene->FixedUpdateEvent.AddLambda([this](float deltaSecond)
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
                    FixedUpdate(deltaSecond);
                }
            }
        });
    }
    virtual ~IFixedUpdatable()
    {
        subscribedScene->FixedUpdateEvent.Remove(m_fixedUpdateEventHandle);
    }

    virtual void FixedUpdate(float deltaSecond) = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_fixedUpdateEventHandle{};
};
