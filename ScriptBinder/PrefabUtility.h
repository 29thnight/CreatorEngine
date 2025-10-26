#pragma once
#include "Core.Minimal.h"
#include "DLLAcrossSingleton.h"
#include "Prefab.h"

class PrefabUtility : public DLLCore::Singleton<PrefabUtility>
{
private:
    friend class DLLCore::Singleton<PrefabUtility>;
    PrefabUtility() = default;
    ~PrefabUtility() = default;

public:
    Core::Delegate<void, GameObject&> prefabInstanceUpdated;
    Core::Delegate<void, GameObject&> prefabInstanceApplied;
    Core::Delegate<void, GameObject&> prefabInstanceReverted;
    Core::Delegate<void, GameObject&> prefabInstanceUnpacked;

    Prefab* CreatePrefab(const GameObject* source, std::string_view name = "");
    GameObject* InstantiatePrefab(const Prefab* prefab, std::string_view name = "");
	GameObject* InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name = "");
    void RegisterInstance(GameObject* instance, const Prefab* prefab);
    void UpdateInstances(const Prefab* prefab);
    bool SavePrefab(const Prefab* prefab, const std::string& path);
    Prefab* LoadPrefabFullPath(const std::string& path);
    Prefab* LoadPrefab(const std::string& path);
	Prefab* LoadPrefabGuid(const FileGuid& guid);

private:
    std::unordered_map<FileGuid, std::vector<GameObject*>> m_instanceMap{};
};

static auto PrefabUtilitys = PrefabUtility::GetInstance();
