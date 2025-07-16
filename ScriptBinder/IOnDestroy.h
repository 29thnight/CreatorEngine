#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
#include "Scene.h"

interface IOnDestroy
{
    IOnDestroy()
    {
        subscribedScene = SceneManagers->GetActiveScene();
        if (!subscribedScene)
        {
            return;
        }
        m_OnDestroyEventHandle = subscribedScene->OnDestroyEvent.AddLambda([this]
        {
            auto ptr = dynamic_cast<Component*>(this);
            auto sceneObject = ptr->GetOwner();
            if(ptr && sceneObject && sceneObject->IsDestroyMark())
            {
                OnDestroy();
            }
        });
    }
    virtual ~IOnDestroy()
    {
        subscribedScene->OnDestroyEvent.Remove(m_OnDestroyEventHandle);
    }

    virtual void OnDestroy() = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_OnDestroyEventHandle{};
};
