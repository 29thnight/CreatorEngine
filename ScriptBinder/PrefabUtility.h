#pragma once
#include "Core.Minimal.h"
#include "Prefab.h"

class PrefabUtility : public Singleton<PrefabUtility>
{
private:
    friend class Singleton;
    PrefabUtility() = default;
    ~PrefabUtility() = default;

public:
    Core::Delegate<void, GameObject&> prefabInstanceUpdated;
    Core::Delegate<void, GameObject&> prefabInstanceApplied;
    Core::Delegate<void, GameObject&> prefabInstanceReverted;
    Core::Delegate<void, GameObject&> prefabInstanceUnpacked;

    Prefab* CreatePrefab(const GameObject* source, const std::string_view& name = "");
    GameObject* InstantiatePrefab(const Prefab* prefab, const std::string_view& name = "");
};

