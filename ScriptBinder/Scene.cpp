#include "Scene.h"

std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
	m_SceneObjects.push_back(sceneObject);

	const_cast<GameObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);

	return sceneObject;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string_view& name, GameObject::Index parentIndex)
{
	GameObject::Index index = m_SceneObjects.size();

	m_SceneObjects.push_back(std::make_shared<GameObject>(name, index, parentIndex));
	auto parentObj = GetGameObject(parentIndex);
	if (parentObj->m_index != index)
	{
		parentObj->m_childrenIndices.push_back(index);
	}
	return m_SceneObjects[index];
}

std::shared_ptr<GameObject> Scene::GetGameObject(GameObject::Index index)
{
	if (index < m_SceneObjects.size())
	{
		return m_SceneObjects[index];
	}
	return m_SceneObjects[0];
}

void Scene::Start()
{
}

void Scene::Update(float deltaSecond)
{
}
