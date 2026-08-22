#include "PrefabEditor.h"
#include "Scene.h"
#include "ClrHost.h"

PrefabEditor::PrefabEditor()
{
}

void PrefabEditor::Open(const std::string& path)
{
    if (m_isOpened)
        Close(false);

    m_prefab = PrefabUtilitys->LoadPrefabFullPath(path);
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

    if (apply && m_editScene && m_editScene->m_Entities.size() > 1)
    {
        auto rootObj = m_editScene->m_Entities[1].get();
        Prefab* newPrefab = PrefabUtilitys->CreatePrefab(rootObj);
        newPrefab->SetFileGuid(m_prefab->GetFileGuid());
        // SavePrefab이 같은 경로 키의 캐시 항목(=m_prefab)을 내부에서 지울 수 있다 —
        // 여기서 다시 만지거나 delete하면 이중 해제다. newPrefab도 PrefabUtility가
        // 소유하므로(m_createdPrefabs) 그냥 놓는다.
        PrefabUtilitys->SavePrefab(newPrefab, m_path.string());
        PrefabUtilitys->UpdateInstances(newPrefab);
        m_prefab = nullptr;
    }

    sceneUnloadedEvent.Broadcast();
    if (m_editScene)
    {
        m_editScene->AllDestroyMark();
        m_editScene->EndFramePass();

        // 파괴 뒤에 던진다(ClrHost.h 선언 주석 참고).
        ClrHost::Get().NotifySceneUnload();
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
