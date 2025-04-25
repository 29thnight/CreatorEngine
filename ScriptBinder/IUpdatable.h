#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
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
            Update(deltaSecond);
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
