#pragma once
#include "Object.h"
#include "AssetBundle.h"
#include "ReflectionYml.h"
#include "DLLAcrossSingleton.h"
#include "Core.ThreadPool.h"

class Scene;
class MeshRenderer;
class RenderScene;
class InputActionManager;
class SceneManager : public DLLCore::Singleton<SceneManager>
{
private:
    friend class DLLCore::Singleton<SceneManager>;
    SceneManager() = default;
    ~SceneManager() = default;

public:
	void ManagerInitialize();
    void Editor();
    void Initialization();
    void Physics(float deltaSecond);
    void InputEvents(float deltaSecond);
    void GameLogic(float deltaSecond = 0);
    void SceneRendering(float deltaSecond);
	void OnDrawGizmos();
    void GUIRendering();
    void EndOfFrame();
    void Pausing();
    void DisableOrEnable();
    void Decommissioning();
    void SetDecommissioning();
    bool IsDecommissioning() const { return m_exitCommand; }

    Scene* GetActiveScene() { return m_activeScene; }
    Scene* GetScene(size_t index) { return m_scenes[index]; }

    Scene* CreateScene(std::string_view name = "SampleScene");
	Scene* SaveScene(std::string_view name = "SampleScene");
    Scene* LoadSceneImmediate(std::string_view name = "SampleScene");
	Scene* LoadScene(std::string_view name = "SampleScene");

	void SaveSceneAsync(std::string_view name = "SampleScene");
	std::future<Scene*> LoadSceneAsync(std::string_view name = "SampleScene");
    void LoadSceneAsyncAndWaitCallback(std::string_view name = "SampleScene");
    void ActivateScene(Scene* sceneToActivate, bool isOldSceneDelete = true);

    void WaitForSceneLoad();

    RenderScene* GetRenderScene() { return m_ActiveRenderScene; }
    void SetRenderScene(RenderScene* renderScene) { m_ActiveRenderScene = renderScene; }
    void AddDontDestroyOnLoad(std::shared_ptr<Object> objPtr);
	void RemoveDontDestroyOnLoad(std::shared_ptr<Object> objPtr);
	void RebindEventDontDestroyOnLoadObjects(Scene* scene);
    
	std::vector<Scene*>& GetScenes() { return m_scenes; }
	std::vector<std::shared_ptr<Object>>& GetDontDestroyOnLoadObjects() { return m_dontDestroyOnLoadObjects; }
	void SetActiveScene(Scene* scene) { m_activeScene = scene; }
	void SetActiveSceneIndex(size_t index) { m_activeSceneIndex = index; }
	size_t GetActiveSceneIndex() { return m_activeSceneIndex; }
	bool IsGameStart() const { return m_isGameStart; }
	void SetGameStart(bool isStart);

	bool IsGamePaused() const { return m_isGamePaused; }
	void SetGamePaused(bool isPaused);
	void ToggleGamePaused();

	bool IsEditorSceneLoaded() const { return m_isEditorSceneLoaded; }
    InputActionManager* GetInputActionManager() { return m_inputActionManager; }
    void SetInputActionManager(InputActionManager* inputActionManager) { m_inputActionManager = inputActionManager;}

    std::vector<MeshRenderer*> GetAllMeshRenderers() const;

	void VolumeProfileApply();
	bool IsVolumeProfileApply() const { return m_volumeProfileApply; }
	void ResetVolumeProfileApply() { m_volumeProfileApply = false; }

public:
	//for Editor
	Core::Delegate<void>                PlayModeEvent{};
    //for Game Logic
    Core::Delegate<void, float>         InputEvent{};
    //for RenderEngine
    Core::Delegate<void, float>         SceneRenderingEvent{};
	Core::Delegate<void>                OnDrawGizmosEvent{};
    Core::Delegate<void>                GUIRenderingEvent{};
    Core::Delegate<void, float>         InternalAnimationUpdateEvent{};
    //Manager Events
    Core::Delegate<void>                activeSceneChangedEvent{};
    Core::Delegate<void>                sceneLoadedEvent{};
    Core::Delegate<void>                sceneUnloadedEvent{};
    Core::Delegate<void>                newSceneCreatedEvent{};
	Core::Delegate<void>                resetSelectedObjectEvent{};
    Core::Delegate<void>                endOfFrameEvent{};
	Core::Delegate<void>                resourceTrimEvent{};
    
	Core::Delegate<void>                AssetLoadEvent{};

    std::atomic_bool                    m_isGameStart{ false };
    std::atomic_bool                    m_isGamePaused{ false };
	std::atomic_bool			        m_isEditorSceneLoaded{ false };
	std::atomic_bool                    m_isInitialized{ false };
	size_t 					            m_EditorSceneIndex{ 0 };
    std::atomic_bool                    m_loadSceneReturn{ false };
    ThreadPool<std::function<void()>>*  m_threadPool{ nullptr };

    std::future<Scene*>                 m_loadingSceneFuture;
    InputActionManager*                 m_inputActionManager{ nullptr };  //TODO: 삭제처리 없음 필요시 추가해야함 //sehwan&&&&&
private:
    void CreateEditorOnlyPlayScene();
	void DeleteEditorOnlyPlayScene();
    void DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);
    void DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);
	void DesirealizeDontDestroyOnLoadObjects(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);
private:
    std::vector<Scene*>                 m_scenes{};
    std::vector<std::shared_ptr<Object>>m_dontDestroyOnLoadObjects{};
    AssetBundle                         m_dontDestroyOnLoadAssetsBundle{};
    std::atomic<Scene*>                 m_activeScene{};
    std::atomic<RenderScene*>           m_ActiveRenderScene{ nullptr };
	std::string                         m_LoadSceneName{};
    std::atomic_size_t                  m_activeSceneIndex{};
	std::atomic_bool                    m_volumeProfileApply{ false };
    std::atomic_bool                    m_exitCommand{ false };
};

static auto SceneManagers = SceneManager::GetInstance();
#pragma region SceneManagerEvents
static auto& PlayModeEvent = SceneManager::GetInstance()->PlayModeEvent;
static auto& InputEvent = SceneManager::GetInstance()->InputEvent;
static auto& SceneRenderingEvent = SceneManager::GetInstance()->SceneRenderingEvent;
static auto& OnDrawGizmosEvent = SceneManager::GetInstance()->OnDrawGizmosEvent;
static auto& GUIRenderingEvent = SceneManager::GetInstance()->GUIRenderingEvent;
static auto& InternalAnimationUpdateEvent = SceneManager::GetInstance()->InternalAnimationUpdateEvent;
static auto& activeSceneChangedEvent = SceneManager::GetInstance()->activeSceneChangedEvent;
static auto& sceneLoadedEvent = SceneManager::GetInstance()->sceneLoadedEvent;
static auto& sceneUnloadedEvent = SceneManager::GetInstance()->sceneUnloadedEvent;
static auto& newSceneCreatedEvent = SceneManager::GetInstance()->newSceneCreatedEvent;
static auto& resetSelectedObjectEvent = SceneManager::GetInstance()->resetSelectedObjectEvent;
static auto& endOfFrameEvent = SceneManager::GetInstance()->endOfFrameEvent;
static auto& resourceTrimEvent = SceneManager::GetInstance()->resourceTrimEvent;
#pragma endregion