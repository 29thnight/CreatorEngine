#pragma once
#include "Core.Minimal.h"

class Scene;
class Object;
class SceneManager : public Singleton<SceneManager>
{
private:
    friend class Singleton;
    SceneManager() = default;
    ~SceneManager() = default;

public:
    void Editor();
    void Initialization();
    void Physics(float deltaSecond);
    void InputEvents(float deltaSecond);
    void GameLogic(float deltaSecond);
    void SceneRendering(float deltaSecond);
    void GUIRendering();
    void EndOfFrame();
    void Pausing();
    void DisableOrEnable();
    void Deccommissioning();

    Scene* GetActiveScene() { return m_activeScene; }
    Scene* GetScene(size_t index) { return m_scenes[index]; }

    Scene* CreateScene(const std::string_view& name = "SampleScene");
    Scene* LoadScene(const std::string_view& name);
    Scene* LoadSceneAsync(const std::string_view& name);

    void AddDontDestroyOnLoad(Object* objPtr);

public:
    //for Game Logic
    Core::Delegate<void, float> InputEvent{};
    //for RenderEngine
    Core::Delegate<void, float> SceneRenderingEvent{};
    Core::Delegate<void>        GUIRenderingEvent{};
    Core::Delegate<void, float> InternalAnimationUpdateEvent{};
    //Manager Events
    Core::Delegate<void>        activeSceneChangedEvent{};
    Core::Delegate<void>        sceneLoadedEvent{};
    Core::Delegate<void>        sceneUnloadedEvent{};
    Core::Delegate<void>        newSceneCreatedEvent{};

private:
    std::vector<Scene*>         m_scenes{};
    std::vector<Object*>        m_dontDestroyOnLoadObjects{};
    Scene*                      m_activeScene{};
    std::atomic_size_t          m_activeSceneIndex{};
};

static auto& SceneManagers = SceneManager::GetInstance();
