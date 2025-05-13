#pragma once
#include "Core.Minimal.h"
#include "ReflectionYml.h"

class Scene;
class Object;
class MeshRenderer;
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
    void Deccommissioning();

    Scene* GetActiveScene() { return m_activeScene; }
    Scene* GetScene(size_t index) { return m_scenes[index]; }

    Scene* CreateScene(const std::string_view& name = "SampleScene");
	Scene* SaveScene(const std::string_view& name = "SampleScene", bool isAsync = false);
    Scene* LoadScene(const std::string_view& name = "SampleScene", bool isAsync = false);

    void AddDontDestroyOnLoad(Object* objPtr);
    
	std::vector<Scene*>& GetScenes() { return m_scenes; }
	std::vector<Object*>& GetDontDestroyOnLoadObjects() { return m_dontDestroyOnLoadObjects; }
	void SetActiveScene(Scene* scene) { m_activeScene = scene; }
	void SetActiveSceneIndex(size_t index) { m_activeSceneIndex = index; }
	size_t GetActiveSceneIndex() { return m_activeSceneIndex; }
	bool IsGameStart() const { return m_isGameStart; }
	void SetGameStart(bool isStart) { m_isGameStart = isStart; }

    std::vector<MeshRenderer*> GetAllMeshRenderers() const;

public:
	//for Editor
	Core::Delegate<void>        PlayModeEvent{};
    //for Game Logic
    Core::Delegate<void, float> InputEvent{};
    //for RenderEngine
    Core::Delegate<void, float> SceneRenderingEvent{};
	Core::Delegate<void>        OnDrawGizmosEvent{};
    Core::Delegate<void>        GUIRenderingEvent{};
    Core::Delegate<void, float> InternalAnimationUpdateEvent{};
    //Manager Events
    Core::Delegate<void>        activeSceneChangedEvent{};
    Core::Delegate<void>        sceneLoadedEvent{};
    Core::Delegate<void>        sceneUnloadedEvent{};
    Core::Delegate<void>        newSceneCreatedEvent{};
	Core::Delegate<void>        resetSelectedObjectEvent{};

    bool                        m_isGameStart{ false };
	size_t 					    m_EditorSceneIndex{ 0 };

private:
    void CreateEditorOnlyPlayScene();
	void DeleteEditorOnlyPlayScene();
    void DesirealizeGameObject(const Meta::Type* type, const MetaYml::detail::iterator_value& itNode);

private:
    std::vector<Scene*>         m_scenes{};
    std::vector<Object*>        m_dontDestroyOnLoadObjects{};
    Scene*                      m_activeScene{};
	std::string                 m_LoadSceneName{};
    std::atomic_size_t          m_activeSceneIndex{};
	bool						m_isEditorSceneLoaded{ false };
};

static auto& SceneManagers = SceneManager::GetInstance();
