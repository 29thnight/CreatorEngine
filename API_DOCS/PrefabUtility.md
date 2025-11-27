# PrefabUtility

**Header:** `ScriptBinder/PrefabUtility.h`

**Inheritance:** `: public DLLCore::Singleton<PrefabUtility>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `Prefab* CreatePrefab(const GameObject* source, std::string_view name = "");`
- `GameObject* InstantiatePrefab(const Prefab* prefab, std::string_view name = "");`
- `GameObject* InstantiatePrefab(const Prefab* prefab, Scene* targetScene, std::string_view name = "");`
- `void RegisterInstance(GameObject* instance, const Prefab* prefab);`
- `void UpdateInstances(const Prefab* prefab);`
- `bool SavePrefab(const Prefab* prefab, const std::string& path);`
- `Prefab* LoadPrefabFullPath(const std::string& path);`
- `Prefab* LoadPrefab(const std::string& path);`
- `Prefab* LoadPrefabGuid(const FileGuid& guid);`

## Public Properties
- `Core::Delegate<void, GameObject&> prefabInstanceUpdated;`
- `Core::Delegate<void, GameObject&> prefabInstanceApplied;`
- `Core::Delegate<void, GameObject&> prefabInstanceReverted;`
- `Core::Delegate<void, GameObject&> prefabInstanceUnpacked;`
