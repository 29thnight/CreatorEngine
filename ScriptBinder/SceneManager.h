#pragma once
#include "Core.Minimal.h"
#include "ReflectionYml.h"
#include "Core.ThreadPool.h"

class Scene;
class Object;
class MeshRenderer;
class RenderScene;
class InputActionManager;
class SceneManager : public Singleton<SceneManager>
{
private:
    friend class Singleton;
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

    Scene* GetActiveScene() { return m_activeScene; }
    Scene* GetScene(size_t index) { return m_scenes[index]; }

    Scene* CreateScene(const std::string_view& name = "SampleScene");
	Scene* SaveScene(const std::string_view& name = "SampleScene");
    Scene* LoadScene(const std::string_view& name = "SampleScene");

	void SaveSceneAsync(const std::string_view& name = "SampleScene");
    std::future<Scene*> LoadSceneAsync(const std::string_view& name = "SampleScene");

	void ActivateScene(Scene* sceneToActivate);

    RenderScene* GetRenderScene() { return m_ActiveRenderScene; }
    void SetRenderScene(RenderScene* renderScene) { m_ActiveRenderScene = renderScene; }
    void AddDontDestroyOnLoad(Object* objPtr);
    
	std::vector<Scene*>& GetScenes() { return m_scenes; }
	std::vector<Object*>& GetDontDestroyOnLoadObjects() { return m_dontDestroyOnLoadObjects; }
	void SetActiveScene(Scene* scene) { m_activeScene = scene; }
	void SetActiveSceneIndex(size_t index) { m_activeSceneIndex = index; }
	size_t GetActiveSceneIndex() { return m_activeSceneIndex; }
	bool IsGameStart() const { return m_isGameStart; }
	void SetGameStart(bool isStart) { m_isGameStart = isStart; }

	bool IsEditorSceneLoaded() const { return m_isEditorSceneLoaded; }
    InputActionManager* GetInputActionManager() { return m_inputActionManager; }
    void SetInputActionManager(InputActionManager* inputActionManager) { m_inputActionManager = inputActionManager;}

    std::vector<MeshRenderer*> GetAllMeshRenderers() const;

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
	std::atomic_bool			        m_isEditorSceneLoaded{ false };
	std::atomic_bool                    m_isInitialized{ false };
	size_t 					            m_EditorSceneIndex{ 0 };

    ThreadPool<std::function<void()>>*  m_threadPool{ nullptr };

    std::future<Scene*>                 m_loadingSceneFuture;
    InputActionManager*                 m_inputActionManager{ nullptr };  //TODO: 삭제처리 없음 필요시 추가해야함 //sehwan&&&&&
private:
    void CreateEditorOnlyPlayScene();
	void DeleteEditorOnlyPlayScene();
    void DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);
    void DesirealizeGameObject(Scene* targetScene, const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);

private:
    std::vector<Scene*>                 m_scenes{};
    std::vector<Object*>                m_dontDestroyOnLoadObjects{};
    std::atomic<Scene*>                 m_activeScene{};
    std::atomic<RenderScene*>           m_ActiveRenderScene{ nullptr };
	std::string                         m_LoadSceneName{};
    std::atomic_size_t                  m_activeSceneIndex{};
};

static auto& SceneManagers = SceneManager::GetInstance();
#pragma region SceneManagerEvents
static auto& PlayModeEvent = SceneManagers->PlayModeEvent;
static auto& InputEvent = SceneManagers->InputEvent;
static auto& SceneRenderingEvent = SceneManagers->SceneRenderingEvent;
static auto& OnDrawGizmosEvent = SceneManagers->OnDrawGizmosEvent;
static auto& GUIRenderingEvent = SceneManagers->GUIRenderingEvent;
static auto& InternalAnimationUpdateEvent = SceneManagers->InternalAnimationUpdateEvent;
static auto& activeSceneChangedEvent = SceneManagers->activeSceneChangedEvent;
static auto& sceneLoadedEvent = SceneManagers->sceneLoadedEvent;
static auto& sceneUnloadedEvent = SceneManagers->sceneUnloadedEvent;
static auto& newSceneCreatedEvent = SceneManagers->newSceneCreatedEvent;
static auto& resetSelectedObjectEvent = SceneManagers->resetSelectedObjectEvent;
static auto& endOfFrameEvent = SceneManagers->endOfFrameEvent;
static auto& resourceTrimEvent = SceneManagers->resourceTrimEvent;
#pragma endregion