#include "PrefabEditor.h"
#include "Scene.h"
#include "ClrHost.h"
#include "ConsoleCommandSystem.h"
#include <algorithm>
#include <fstream>
#include <memory>
#include "ReflectionYml.h"

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
    m_observedPayload.clear();
    m_savedPayload.clear();
    m_attemptedPayload.clear();
    std::ifstream input(m_path, std::ios::binary);
    m_diskPayload.assign(std::istreambuf_iterator<char>(input), {});
    m_nextPoll = {};
    TickAutomaticSave();
}

void PrefabEditor::TickAutomaticSave(bool flush)
{
    const auto now = std::chrono::steady_clock::now();
    if (!m_isOpened || (!flush && now < m_nextPoll))
    {
        return;
    }
    m_nextPoll = now + std::chrono::milliseconds(200);
    auto* scene = ResolvePrefabEditorScene(m_editSceneId);
    if (!scene || scene->m_Entities.size() < 2 || !scene->m_Entities[1])
    {
        return;
    }
    auto candidate = std::unique_ptr<Prefab>(Prefab::CreateFromGameObject(scene->m_Entities[1].get()));
    if (!candidate)
    {
        return;
    }
    candidate->SetFileGuid(m_prefabGuid);
    // A temporary Prefab receives a new object instance ID on every poll.
    // Compare the authored entity tree so that runtime bookkeeping cannot
    // prevent the debounce from settling or trigger a save by itself.
    const auto payload = candidate->GetPrefabData().Dump();
    if (m_observedPayload.empty())
    {
        m_observedPayload = m_savedPayload = payload;
        return;
    }
    if (payload != m_observedPayload)
    {
        m_observedPayload = payload;
        m_changedAt = now;
        if (!flush)
        {
            return;
        }
    }
    if (payload == m_savedPayload || payload == m_attemptedPayload
        || (!flush && now - m_changedAt < std::chrono::milliseconds(600)))
    {
        return;
    }
    m_attemptedPayload = payload;
    std::ifstream input(m_path, std::ios::binary);
    const std::string disk{ std::istreambuf_iterator<char>(input), {} };
    // Windows atomic replacement requires the comparison handle to be closed.
    input.close();
    if (!input || disk != m_diskPayload)
    {
        Debug::PrintLog(spdlog::level::err, "Prefab automatic save retained edits: the source changed on disk.");
        return;
    }
    if (!PrefabUtilitys->SavePrefab(candidate.get(), m_path.string()))
    {
        Debug::PrintLog(spdlog::level::err, "Prefab automatic save failed; the editing scene is retained.");
        return;
    }
    // Update uses the stable cache-owned definition, never the temporary candidate.
    if (auto* accepted = PrefabUtilitys->LoadPrefabFullPath(m_path.string()))
    {
        try
        {
            PrefabUtilitys->UpdateInstances(accepted, scene);
        }
        catch (const std::exception& error)
        {
            Debug::PrintLog(spdlog::level::err, std::string("Prefab automatic Apply failed; editing scene retained: ") + error.what());
        }
    }
    std::ifstream saved(m_path, std::ios::binary);
    m_diskPayload.assign(std::istreambuf_iterator<char>(saved), {});
    m_savedPayload = payload;
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
            if (!PrefabUtilitys->SavePrefab(prefab, m_path.string()))
            {
                Debug::PrintLog(spdlog::level::err, "Prefab save failed; the editing scene is retained.");
                return;
            }
            PrefabUtilitys->UpdateInstances(prefab, editScene);
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
