#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Renderer.h"

GameObject::GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex)
{
	std::cout << "Vector Address: " << &this->m_components << std::endl;
}

GameObject* GameObject::Find(const std::string_view& name)
{
    Scene* scene = SceneManagers->GetActiveScene();
    if (scene)
    {
        return scene->GetGameObject(name).get();
    }
    return nullptr;
}
