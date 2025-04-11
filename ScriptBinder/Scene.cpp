#include "Scene.h"
#include "HotLoadSystem.h"
#include "IlifeSycle.h"
std::shared_ptr<GameObject> Scene::AddGameObject(const std::shared_ptr<GameObject>& sceneObject)
{
	m_SceneObjects.push_back(sceneObject);

	const_cast<GameObject::Index&>(sceneObject->m_index) = m_SceneObjects.size() - 1;

	m_SceneObjects[0]->m_childrenIndices.push_back(sceneObject->m_index);
	sceneObject->SetScene(this);

	return sceneObject;
}

std::shared_ptr<GameObject> Scene::CreateGameObject(const std::string_view& name, GameObject::Type type, GameObject::Index parentIndex)
{
	GameObject::Index index = m_SceneObjects.size();

	m_SceneObjects.push_back(std::make_shared<GameObject>(name, type, index, parentIndex));
	auto parentObj = GetGameObject(parentIndex);
	if (parentObj->m_index != index)
	{
		parentObj->m_childrenIndices.push_back(index);
	}
	m_SceneObjects.back()->SetScene(this);

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

std::shared_ptr<GameObject> Scene::GetGameObject(const std::string_view& name)
{
	HashingString hashedName(name.data());
	for (auto& obj : m_SceneObjects)
	{
		if (obj->GetHashedName() == hashedName)
		{
			return obj;
		}
	}
	return nullptr;
}

void Scene::Start()
{
	for (auto& obj : m_SceneObjects)
	{
		if (obj->GetType() == GameObject::Type::Empty)
		{
			continue;
		}
	}
	m_isPlaying = true;
}

void Scene::FixedUpdate(float deltaSecond)
{
}

void Scene::OnTriggerEnter(ICollider* other)
{
}

void Scene::OnTriggerStay(ICollider* other)
{
}

void Scene::OnTriggerExit(ICollider* other)
{
}

void Scene::OnCollisionEnter(ICollider* other)
{
}

void Scene::OnCollisionStay(ICollider* other)
{
}

void Scene::OnCollisionExit(ICollider* other)
{
}

void Scene::Update(float deltaSecond)
{
	for (auto& obj : m_SceneObjects)
	{
		obj->Update(deltaSecond);
	}

}

void Scene::YieldNull()
{
	ScriptManager->ReplaceScriptComponent();
}

void Scene::LateUpdate(float deltaSecond)
{
}

void Scene::OnDisable()
{
}

void Scene::OnDestroy()
{
}

