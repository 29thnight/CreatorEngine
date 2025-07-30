#include "PrefabEditor.h"

PrefabEditor::PrefabEditor()
{
}

void PrefabEditor::Open(const std::string& path)
{
    if (m_isOpened)
        Close(false);

    m_prefab = PrefabUtilitys->LoadPrefab(path);
    if (!m_prefab)
        return;

    m_path = path;
    m_prevScene = SceneManagers->GetActiveScene();
    m_prevSceneIndex = SceneManagers->GetActiveSceneIndex();

    m_editScene = Scene::CreateNewScene("PrefabEditor");
    SceneManagers->GetScenes().push_back(m_editScene);
    SceneManagers->SetActiveScene(m_editScene);
    SceneManagers->SetActiveSceneIndex(SceneManagers->GetScenes().size() - 1);
    activeSceneChangedEvent.Broadcast();
    sceneLoadedEvent.Broadcast();

    PrefabUtilitys->InstantiatePrefab(m_prefab);

    m_isOpened = true;
}

void PrefabEditor::Close(bool apply)
{
    if (!m_isOpened)
        return;

    if (apply && m_editScene && m_editScene->m_SceneObjects.size() > 1)
    {
        auto rootObj = m_editScene->m_SceneObjects[1].get();
        Prefab* newPrefab = PrefabUtilitys->CreatePrefab(rootObj);
        newPrefab->SetFileGuid(m_prefab->GetFileGuid());
        PrefabUtilitys->SavePrefab(newPrefab, m_path.string());
        PrefabUtilitys->UpdateInstances(newPrefab);
        delete m_prefab;
        m_prefab = newPrefab;
    }

    sceneUnloadedEvent.Broadcast();
    if (m_editScene)
    {
        m_editScene->AllDestroyMark();
        m_editScene->OnDisable();
        m_editScene->OnDestroy();
        auto& scenes = SceneManagers->GetScenes();
        std::erase_if(scenes, [&](auto* s) { return s == m_editScene; });
        delete m_editScene;
        m_editScene = nullptr;
    }

    SceneManagers->SetActiveSceneIndex(m_prevSceneIndex);
    SceneManagers->SetActiveScene(m_prevScene);
    activeSceneChangedEvent.Broadcast();

    m_isOpened = false;
}
