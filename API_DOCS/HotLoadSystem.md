# HotLoadSystem

**Header:** `ScriptBinder/HotLoadSystem.h`

**Inheritance:** `: public DLLCore::Singleton<HotLoadSystem>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Initialize();`
- `void Shutdown();`
- `bool IsScriptUpToDate();`
- `void ReloadDynamicLibrary();`
- `void ReplaceScriptComponent();`
- `void ReplaceScriptComponentTargetScene(Scene* targetScene);`
- `void CompileEvent();`
- `void CreateScriptFile(std::string_view name);`
- `void BindScriptEvents(ModuleBehavior* script, std::string_view name);`
- `void UnbindScriptEvents(ModuleBehavior* script, std::string_view name);`
- `void RegisterScriptReflection(std::string_view name, ModuleBehavior* script);`
- `void UnRegisterScriptReflection(std::string_view name);`
- `void RecollectScriptComponents(const std::vector<std::shared_ptr<GameObject>>& gameObjects);`
- `void CreateActionNodeScript(std::string_view name);`
- `void CreateConditionNodeScript(std::string_view name);`
- `void CreateConditionDecoratorNodeScript(std::string_view name);`
- `void CreateAniBehaviorScript(std::string_view name);`
- `void CollectScriptComponent(GameObject* gameObject, size_t index, const std::string& name);`
- `void UnCollectScriptComponent(GameObject* gameObject, size_t index, const std::string& name);`
- `void ResetAniBehaviorPtr();`

## Public Properties
- (none)
