#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Component.h"
#include "GameObject.h"
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
            if(nullptr != ptr && ptr->GetOwner() && ptr->GetOwner()->IsDestroyMark())
            {
                OnDisable();
            }
        });
    }
    virtual ~IOnDisable()
    {
        subscribedScene->UpdateEvent.Remove(m_onDisableEventHandle);
    }

    virtual void OnDisable() = 0;

protected:
    Scene* subscribedScene{};
    Core::DelegateHandle m_onDisableEventHandle{};
};
