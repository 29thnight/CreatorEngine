#pragma once
#include "GameObject.h"
#include "LightProperty.h"
#include "Scene.generated.h"

class GameObject;
class RenderScene;
class SceneManager;
struct ICollider;
class Scene
{
private:

public:
   ReflectScene
    [[Serializable]]
	Scene() = default;
	~Scene() = default;

    [[Property]]
	std::vector<std::shared_ptr<GameObject>> m_SceneObjects;

	std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);
	std::shared_ptr<GameObject> CreateGameObject(const std::string_view& name, GameObject::Type type = GameObject::Type::Empty, GameObject::Index parentIndex = 0);
	std::shared_ptr<GameObject> LoadGameObject(size_t instanceID, const std::string_view& name, GameObject::Type type = GameObject::Type::Empty, GameObject::Index parentIndex = 0);
	std::shared_ptr<GameObject> GetGameObject(GameObject::Index index);
	std::shared_ptr<GameObject> GetGameObject(const std::string_view& name);
	void DestroyGameObject(const std::shared_ptr<GameObject>& sceneObject);
	void DestroyGameObject(GameObject::Index index);

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
    Core::Delegate<void, float>      FixedUpdateEvent{};
    Core::Delegate<void, ICollider*> OnTriggerEnterEvent{};
    Core::Delegate<void, ICollider*> OnTriggerStayEvent{};
    Core::Delegate<void, ICollider*> OnTriggerExitEvent{};
    Core::Delegate<void, ICollider*> OnCollisionEnterEvent{};
	Core::Delegate<void, ICollider*> OnCollisionStayEvent{};
	Core::Delegate<void, ICollider*> OnCollisionExitEvent{};

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
    void OnTriggerEnter(ICollider* collider);
    void OnTriggerStay(ICollider* collider);
    void OnTriggerExit(ICollider* collider);
    void OnCollisionEnter(ICollider* collider);
    void OnCollisionStay(ICollider* collider);
    void OnCollisionExit(ICollider* collider);

    //Game logic
    void Update(float deltaSecond);
    void YieldNull();
    void LateUpdate(float deltaSecond);

    //Disable or Enable
    void OnDisable();
	void OnDestroy();

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

public:
    //TODO : 진짜 이렇게 구현할건지 고민 좀 해보자
    void UpdateLight(LightProperties& lightProperties) const
    {
        lightProperties.m_eyePosition = m_lightProperties.m_eyePosition;
        lightProperties.m_globalAmbient = m_lightProperties.m_globalAmbient;
        for (int i = 0; i < MAX_LIGHTS; ++i)
        {
            lightProperties.m_lights[i] = m_lightProperties.m_lights[i];
        }
    }

    int AddLightCount()
    {
        if (m_lightCount >= MAX_LIGHTS)
        {
            m_lightCount = MAX_LIGHTS;
        }

        return m_lightCount++;
    }

    LightProperties& GetLightProperties()
    {
        return m_lightProperties;
    }

  //  ReflectionField(Scene)
  //  {
		//PropertyField
		//({
		//	meta_property(m_sceneName)
		//	meta_property(m_buildIndex)
  //          meta_property(m_SceneObjects)
		//});
		//	
		//FieldEnd(Scene, PropertyOnly)
  //  }

private:
	friend class SceneManager;
    std::string GenerateUniqueGameObjectName(const std::string_view& name)
    {
        std::string uniqueName{ name.data() };
        std::string baseName{ name.data() };
        int count = 1;
        while (m_gameObjectNameSet.find(uniqueName) != m_gameObjectNameSet.end())
        {
            uniqueName = baseName + std::string(" (") + std::to_string(count++) + std::string(")");
        }
        m_gameObjectNameSet.insert(uniqueName);
        return uniqueName;
    }

private:
    std::unordered_set<std::string> m_gameObjectNameSet{};
    LightProperties m_lightProperties;
    [[Property]]
	HashingString m_sceneName;
    int m_lightCount = 0;
};
