# SceneManager

**Header:** `ScriptBinder/SceneManager.h`

**Inheritance:** `: public DLLCore::Singleton<SceneManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods

### Public Methods 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `ManagerInitialize` | manager initialize 동작을 수행합니다. |
| `Editor` | editor 동작을 수행합니다. |
| `Initialization` | initialization 동작을 수행합니다. |
| `Physics` | physics 동작을 수행합니다. |
| `InputEvents` | input events 동작을 수행합니다. |
| `GameLogic` | game logic 동작을 수행합니다. |
| `SceneRendering` | scene rendering 동작을 수행합니다. |
| `OnDrawGizmos` | on draw gizmos 동작을 수행합니다. |
| `GUIRendering` | guirendering 동작을 수행합니다. |
| `EndOfFrame` | end of frame 동작을 수행합니다. |
| `Pausing` | pausing 동작을 수행합니다. |
| `DisableOrEnable` | or enable을(를) 비활성화합니다. |
| `Decommissioning` | decommissioning 동작을 수행합니다. |
| `SetDecommissioning` | decommissioning을(를) 설정합니다. |
| `CreateScene` | scene을(를) 생성합니다. |
| `SaveScene` | scene을(를) 저장합니다. |
| `LoadSceneImmediate` | scene immediate을(를) 불러옵니다. |
| `LoadScene` | scene을(를) 불러옵니다. |
| `SaveSceneAsync` | scene async을(를) 저장합니다. |
| `LoadSceneAsync` | scene async을(를) 불러옵니다. |
| `LoadSceneAsyncAndWaitCallback` | scene async and wait callback을(를) 불러옵니다. |
| `ActivateScene` | activate scene 동작을 수행합니다. |
| `BeforeAwakeSceneLoad` | before awake scene load 동작을 수행합니다. |
| `IsSceneLoading` | scene loading 여부를 확인합니다. |
| `WaitForSceneLoad` | wait for scene load 동작을 수행합니다. |
| `AddDontDestroyOnLoad` | dont destroy on load을(를) 추가합니다. |
| `RemoveDontDestroyOnLoad` | dont destroy on load을(를) 제거합니다. |
| `RebindEventDontDestroyOnLoadObjects` | rebind event dont destroy on load objects 동작을 수행합니다. |
| `SetGameStart` | game start을(를) 설정합니다. |
| `SetGamePaused` | game paused을(를) 설정합니다. |
| `ToggleGamePaused` | toggle game paused 동작을 수행합니다. |
| `GetAllMeshRenderers` | all mesh renderers을(를) 가져옵니다. |
| `VolumeProfileApply` | volume profile apply 동작을 수행합니다. |
## Public Properties

### Public Properties 역할
| 멤버 | 예상 역할 |
| --- | --- |
| `m_loadingSceneFuture` | m loading scene future 상태를 보관합니다. |
