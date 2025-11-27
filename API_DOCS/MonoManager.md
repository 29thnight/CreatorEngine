# MonoManager

**Header:** `ScriptBinder/MonoManager.h`

**Inheritance:** `: public DLLCore::Singleton<MonoManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void Shutdown();`
- `std::optional<AssemblyPack> LoadAssembly(const std::string& name, const std::string& path);`
- `void UnloadAllAssemblies();`
- `bool ReloadAll(const std::vector<std::pair<std::string, std::string>>& assemblies);`
- `MonoThread* AttachCurrentThread();`
- `void        DetachCurrentThread();`
- `void RegisterInternalCalls();`
- `MonoClass* GetClass(const char* nameSpace, const char* klassName, MonoImage* image = nullptr) const;`
- `MonoMethod* GetMethod(MonoClass* klass, const char* methodName, int paramCount) const;`
- `MonoObject* InvokeStatic(MonoClass* klass, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);`
- `MonoObject* CreateInstance(MonoClass* klass);`
- `MonoObject* Invoke(MonoObject* instance, const char* methodName, void** args, int paramCount, MonoObject** outException = nullptr);`
- `MonoString* ToMonoString(const std::string& s) const;`
- `std::string FromMonoString(MonoString* ms) const;`
- `static std::string FormatException(MonoObject* exception);`
- `void GCCollect();`
- `void GCWaitForPendingFinalizers();`
- `MonoImage* GetImage(const std::string& assemblyName) const;`
- `void BindScriptEvents(CSharpScriptComponent* component);`
- `void UnbindScriptEvents(CSharpScriptComponent* component);`

## Public Properties
- `bool enableDebug = false);`
