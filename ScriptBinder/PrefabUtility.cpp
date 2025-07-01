#include "PrefabUtility.h"

Prefab* PrefabUtility::CreatePrefab(const GameObject* source, const std::string_view& name)
{
    return Prefab::CreateFromGameObject(source, name);
}

GameObject* PrefabUtility::InstantiatePrefab(const Prefab* prefab, const std::string_view& name)
{
    if (!prefab)
        return nullptr;
    return prefab->Instantiate(name);
}

