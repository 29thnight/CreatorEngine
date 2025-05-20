#pragma once
#include "GameObject.h"
#include "LightProperty.h"
#include "PhysicsManager.h"
#include "Scene.generated.h"

class GameObject;
class RenderScene;
class SceneManager;
class LightComponent;
class MeshRenderer;
struct ICollider;
class Scene
{
public:
   ReflectScene
    [[Serializable]]
	Scene() = default;
	~Scene() = default;

    [[Property]]
	std::vector<std::shared_ptr<GameObject>> m_SceneObjects;

	std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);
	std::shared_ptr<GameObject> CreateGameObject(const std::string_view& name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = 0);
	std::shared_ptr<GameObject> LoadGameObject(size_t instanceID, const std::string_view& name, GameObjectType type = GameObjectType::Empty, GameObject::Index parentIndex = 0);
	std::shared_ptr<GameObject> GetGameObject(GameObject::Index index);
	std::shared_ptr<GameObject> GetGameObject(const std::string_view& name);
	void DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject);
	void DestroyGameObject(GameObject::Index index);

	inline void InsertGameObjects(std::vector<std::shared_ptr<GameObject>>& gameObjects)
	{
		m_SceneObjects.insert(m_SceneObjects.end(), gameObjects.begin(), gameObjects.end());
	}

private:
    friend class SceneManager;
    //for Editor
    void Reset();

public:
    //Events
    //Initialization
    Core::Delegate<void> AwakeEvent{};
    Core::Delegate<void> OnEnableEvent{};
    Core::Delegate<void> StartEvent{};

    //Physics
    Core::Delegate<void, float>            FixedUpdateEvent{};
	Core::Delegate<void, float>            InternalPhysicsUpdateEvent{};
    Core::Delegate<void, const Collision&> OnTriggerEnterEvent{};
    Core::Delegate<void, const Collision&> OnTriggerStayEvent{};
    Core::Delegate<void, const Collision&> OnTriggerExitEvent{};
    Core::Delegate<void, const Collision&> OnCollisionEnterEvent{};
	Core::Delegate<void, const Collision&> OnCollisionStayEvent{};
	Core::Delegate<void, const Collision&> OnCollisionExitEvent{};

    //Game logic
    Core::Delegate<void, float> UpdateEvent{};
    Core::Delegate<void, float> LateUpdateEvent{};

    //Disable or Enable
    Core::Delegate<void> OnDisableEvent{};
    Core::Delegate<void> OnDestroyEvent{};

public:
    //EventBroadcaster
    //Initialization
    void Awake();
    void OnEnable();
    void Start();

    //Physics
    void FixedUpdate(float deltaSecond);
    void OnTriggerEnter(const Collision& collider);
    void OnTriggerStay(const Collision& collider);
    void OnTriggerExit(const Collision& collider);
    void OnCollisionEnter(const Collision& collider);
    void OnCollisionStay(const Collision& collider);
    void OnCollisionExit(const Collision& collider);

    //Game logic
    void Update(float deltaSecond);
    void YieldNull();
    void LateUpdate(float deltaSecond);

    //Disable or Enable
    void OnDisable();
	void OnDestroy();

    void AllDestroyMark();

	static Scene* CreateNewScene(const std::string_view& sceneName = "SampleScene")
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = sceneName.data();
		allocScene->CreateGameObject(sceneName);
		return allocScene;
	}

	static Scene* LoadScene(const std::string_view& name)
	{
		Scene* allocScene = new Scene();
		allocScene->m_sceneName = name.data();
		return allocScene;
	}

    std::atomic_bool m_isAwake{ false };
    std::atomic_bool m_isLoaded{ false };
    std::atomic_bool m_isDirty{ false };
    std::atomic_bool m_isEnable{ false };
    [[Property]]
    size_t m_buildIndex{ 0 };
    [[Property]]
	HashingString m_sceneName;


public:
	void CollectLightComponent(LightComponent* ptr);
	void UnCollectLightComponent(LightComponent* ptr);
    uint32 UpdateLight(LightProperties& lightProperties) const;
    std::pair<size_t, Light&> AddLight();
	Light& GetLight(size_t index);
    void RemoveLight(size_t index);
	void DistroyLight();

public:
	void CollectMeshRenderer(MeshRenderer* ptr);
	void UnCollectMeshRenderer(MeshRenderer* ptr);
	std::vector<MeshRenderer*>& GetMeshRenderers() { return m_meshRenderers; }
	std::vector<MeshRenderer*>& GetSkinnedMeshRenderers() { return m_skinnedMeshRenderers; }

private:
    void DestroyGameObjects();
	void DestroyComponents();
    std::string GenerateUniqueGameObjectName(const std::string_view& name);
	void RemoveGameObjectName(const std::string_view& name);

private:
    std::unordered_set<std::string> m_gameObjectNameSet{};
	std::vector<LightComponent*>    m_lightComponents;
	std::vector<MeshRenderer*>      m_meshRenderers;
	std::vector<MeshRenderer*>      m_skinnedMeshRenderers;
	std::vector<Light>              m_lights;
};
