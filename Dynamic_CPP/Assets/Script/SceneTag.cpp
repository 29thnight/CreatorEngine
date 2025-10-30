#include "SceneTag.h"
#include "GameInstance.h"
#include "pch.h"

void SceneTag::Awake()
{
	if (m_sceneTag.empty())
        return;

    int currentScene = GameInstance::GetInstance()->GetCurrentSceneType();
    if (currentScene == (int)SceneType::Bootstrap)
    {
        int tagScene = GameInstance::GetInstance()->FindSceneTypeByName(m_sceneTag);
        if (tagScene != -1)
        {
            GameInstance::GetInstance()->SetCurrentSceneType(tagScene);
        }
    }
}

