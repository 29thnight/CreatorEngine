#pragma once
#include "Core.Minimal.h"
#include "SceneManager.h"
#include "Scene.h"

template<typename T>
interface IUpdatable
{
    IUpdatable()
    {
        Scene* currentScene = SceneManagers->GetActiveScene();
        if (!currentScene)
        {
            return;
        }
        m_updateEventHandle = currentScene->UpdateEvent.AddLambda([this](float deltaSecond)
        {
            Update(deltaSecond);
        });
    }
    virtual ~IUpdatable()
    {
        SceneManagers->GetActiveScene()->UpdateEvent.Remove(m_updateEventHandle);
    }

    virtual void Update(float deltaSecond) = 0;

protected:
    Core::DelegateHandle m_updateEventHandle{};
};
