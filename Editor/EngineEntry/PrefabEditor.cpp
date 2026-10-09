#include "PrefabEditor.h"
#include "Scene.h"
#include "ClrHost.h"
#include "ConsoleCommandSystem.h"
#include <algorithm>

namespace
{
    Scene* ResolvePrefabEditorScene(std::uint32_t sceneId)
    {
        for (auto* scene : SceneManagers->GetScenes())
        {
            if (scene && scene->GetSceneId() == sceneId)
            {
                return scene;
            }
        }
        return nullptr;
    }
}

PrefabEditor::PrefabEditor()
{
}

void PrefabEditor::Open(const std::string& path)
{
    if (!ConsoleCommandSystem::Get().IsGameThread())
    {
        ConsoleCommandSystem::Get().EnqueueEditorMutation([path]
        {
            PrefabEditors->Open(path);
            return CommandCore::Ok();
        });
        return;
    }
    if (m_isOpened)
    {
        Close(false);
    }

    auto* prefab = PrefabUtilitys->LoadPrefabFullPath(path);
    if (!prefab)
    {
        return;
    }

    m_path = path;
    m_prefabGuid = prefab->GetFileGuid();
    auto* previous = SceneManagers->GetActiveScene();
    m_previousSceneId = previous ? previous->GetSceneId() : 0;

    auto* editScene = SceneManagers->AdoptScene(
        Scene::CreateNewScene(SceneManagers->ManagedDomain(), "PrefabEditor"));
    m_editSceneId = editScene->GetSceneId();
    SceneManagers->SetActiveScene(editScene);
    SceneManagers->SetActiveSceneIndex(SceneManagers->GetScenes().size() - 1);
    activeSceneChangedEvent.Broadcast();
    sceneLoadedEvent.Broadcast();

    PrefabUtilitys->InstantiatePrefab(prefab);

    m_isOpened = true;
}

void PrefabEditor::Close(bool apply)
{
    if (!ConsoleCommandSystem::Get().IsGameThread())
    {
        ConsoleCommandSystem::Get().EnqueueEditorMutation([apply]
        {
            PrefabEditors->Close(apply);
            return CommandCore::Ok();
        });
        return;
    }
    if (!m_isOpened)
    {
        return;
    }

    auto* editScene = ResolvePrefabEditorScene(m_editSceneId);
    if (!editScene)
    {
        // A normal scene reload may already have retired the prefab scene.
        // Do not dereference an old borrow or replace the new active scene.
        m_isOpened = false;
        m_previousSceneId = 0;
        m_editSceneId = 0;
        m_prefabGuid = {};
        return;
    }
    if (apply && editScene->m_Entities.size() > 1)
    {
        auto* root = editScene->m_Entities[1].get();
        auto* prefab = root ? PrefabUtilitys->CreatePrefab(root) : nullptr;
        if (prefab)
        {
            prefab->SetFileGuid(m_prefabGuid);
            // Save may replace cache-owned assets. Retain only their GUID.
            PrefabUtilitys->SavePrefab(prefab, m_path.string());
            PrefabUtilitys->UpdateInstances(prefab);
        }
    }

    sceneUnloadedEvent.Broadcast();
    SceneManagers->RetireScene(editScene);
    ClrHost::Get().NotifySceneUnload();

    auto* previous = ResolvePrefabEditorScene(m_previousSceneId);
    SceneManagers->SetActiveScene(previous);
    const auto& scenes = SceneManagers->GetScenes();
    const auto position = std::ranges::find(scenes, previous);
    SceneManagers->SetActiveSceneIndex(position == scenes.end() ? 0 : static_cast<std::size_t>(position - scenes.begin()));
    activeSceneChangedEvent.Broadcast();

    m_isOpened = false;
    m_previousSceneId = 0;
    m_editSceneId = 0;
    m_prefabGuid = {};
}
