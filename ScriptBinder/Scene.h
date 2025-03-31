#pragma once
//#include "Camera.h"
//#include "Light.h"
#include "GameObject.h"
//#include "AnimationJob.h"

class RenderScene;
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

	void Start();
	void Update(float deltaSecond);

	static Scene* CreateNewScene()
	{
		//TODO : Scene Pooling
		Scene* allocScene = new Scene();

		return allocScene;
	}

private:
	bool m_isPlaying = false;
};