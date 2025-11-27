# MonoManager

**Header:** `ScriptBinder/MonoManager.h`

**Inheritance:** `: public DLLCore::Singleton<MonoManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `Shutdown` | shutdown 동작을 수행합니다. |
| `LoadAssembly` | assembly을(를) 불러옵니다. |
| `UnloadAllAssemblies` | unload all assemblies 동작을 수행합니다. |
| `ReloadAll` | reload all 동작을 수행합니다. |
| `AttachCurrentThread` | attach current thread 동작을 수행합니다. |
| `DetachCurrentThread` | detach current thread 동작을 수행합니다. |
| `RegisterInternalCalls` | register internal calls 동작을 수행합니다. |
| `GetClass` | class을(를) 가져옵니다. |
| `GetMethod` | method을(를) 가져옵니다. |
| `InvokeStatic` | invoke static 동작을 수행합니다. |
| `CreateInstance` | instance을(를) 생성합니다. |
| `Invoke` | invoke 동작을 수행합니다. |
| `ToMonoString` | to mono string 동작을 수행합니다. |
| `FromMonoString` | from mono string 동작을 수행합니다. |
| `FormatException` | format exception 동작을 수행합니다. |
| `GCCollect` | gccollect 동작을 수행합니다. |
| `GCWaitForPendingFinalizers` | gcwait for pending finalizers 동작을 수행합니다. |
| `GetImage` | image을(를) 가져옵니다. |
| `BindScriptEvents` | bind script events 동작을 수행합니다. |
| `UnbindScriptEvents` | unbind script events 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `enableDebug` | debug을(를) 활성화합니다. |
