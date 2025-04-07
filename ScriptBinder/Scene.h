#pragma once
#include "GameObject.h"
#include "LightProperty.h"

class RenderScene;
struct ICollider;
class Scene
{
public:
	Scene() = default;
	~Scene() = default;
	std::vector<std::shared_ptr<GameObject>> m_SceneObjects;

	std::shared_ptr<GameObject> AddGameObject(const std::shared_ptr<GameObject>& sceneObject);
	std::shared_ptr<GameObject> CreateGameObject(const std::string_view& name, GameObject::Type type = GameObject::Type::Empty, GameObject::Index parentIndex = 0);
	std::shared_ptr<GameObject> GetGameObject(GameObject::Index index);
	std::shared_ptr<GameObject> GetGameObject(const std::string_view& name);

public:
	void Start();

public:
	void FixedUpdate(float deltaSecond);

public:
	void OnTriggerEnter(ICollider* other);
	void OnTriggerStay(ICollider* other);
	void OnTriggerExit(ICollider* other);

public:
	void OnCollisionEnter(ICollider* other);
	void OnCollisionStay(ICollider* other);
	void OnCollisionExit(ICollider* other);

public:
	void Update(float deltaSecond);
	void YieldNull();
	void LateUpdate(float deltaSecond);

public:
	void OnDisable();
	void OnDestroy();

	static Scene* CreateNewScene(const std::string_view& sceneName = "SampleScene")
	{
		Scene* allocScene = new Scene();
		allocScene->CreateGameObject("SampleScene");
		return allocScene;
	}

public:
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

private:
    LightProperties m_lightProperties;
	HashingString m_sceneName;
    int m_lightCount = 0;
	bool m_isPlaying = false;
};
