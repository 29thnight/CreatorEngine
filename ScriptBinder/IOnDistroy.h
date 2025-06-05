#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
#include "Scene.h"

interface IOnDistroy
{
    IOnDistroy()
    {
        subscribedScene = SceneManagers->GetActiveScene();
        if (!subscribedScene)
        {
            return;
        }
        m_onDistroyEventHandle = subscribedScene->OnDestroyEvent.AddLambda([this]
        {
            auto ptr = dynamic_cast<Component*>(this);
            auto sceneObject = ptr->GetOwner();
            if(ptr && sceneObject && sceneObject->IsDestroyMark())
            {
                OnDistroy();
            }
        });
    }
    virtual ~IOnDistroy()
    {
        subscribedScene->OnDestroyEvent.Remove(m_onDistroyEventHandle);
    }

    virtual void OnDistroy() = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_onDistroyEventHandle{};
};
