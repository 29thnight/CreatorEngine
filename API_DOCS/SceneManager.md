# SceneManager

**Header:** `ScriptBinder/SceneManager.h`

**Inheritance:** `: public DLLCore::Singleton<SceneManager>`

> 자동 생성된 문서입니다. 실제 사용 시 구현 파일을 함께 참고하세요.

## Public Methods
- `void ManagerInitialize();`
- `void Editor();`
- `void Initialization();`
- `void Physics(float deltaSecond);`
- `void InputEvents(float deltaSecond);`
- `void GameLogic(float deltaSecond = 0);`
- `void SceneRendering(float deltaSecond);`
- `void OnDrawGizmos();`
- `void GUIRendering();`
- `void EndOfFrame();`
- `void Pausing();`
- `void DisableOrEnable();`
- `void Decommissioning();`
- `void SetDecommissioning();`
- `Scene* CreateScene(std::string_view name = "SampleScene");`
- `Scene* SaveScene(std::string_view name = "SampleScene");`
- `Scene* LoadSceneImmediate(std::string_view name = "SampleScene");`
- `Scene* LoadScene(std::string_view name = "SampleScene");`
- `void SaveSceneAsync(std::string_view name = "SampleScene");`
- `std::future<Scene*> LoadSceneAsync(std::string_view name = "SampleScene");`
- `void LoadSceneAsyncAndWaitCallback(std::string_view name = "SampleScene");`
- `void ActivateScene(Scene* sceneToActivate, bool isOldSceneDelete = true);`
- `void BeforeAwakeSceneLoad();`
- `bool IsSceneLoading() const;`
- `void WaitForSceneLoad();`
- `void AddDontDestroyOnLoad(std::shared_ptr<Object> objPtr);`
- `void RemoveDontDestroyOnLoad(std::shared_ptr<Object> objPtr);`
- `void RebindEventDontDestroyOnLoadObjects(Scene* scene);`
- `void SetGameStart(bool isStart);`
- `void SetGamePaused(bool isPaused);`
- `void ToggleGamePaused();`
- `std::vector<MeshRenderer*> GetAllMeshRenderers() const;`
- `void VolumeProfileApply();`

## Public Properties
- `std::future<Scene*>                 m_loadingSceneFuture;`
