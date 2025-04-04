#pragma once
#include "GameObject.h"

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

private:
	HashingString m_sceneName;
	bool m_isPlaying = false;
};