#pragma once
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "HotLoadSystem.h"

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class ModuleBehavior;
class LightComponent;
class GameObject : public Object, public Meta::IReflectable<GameObject>
{
public:
	using Index = int;

	enum class Type
	{
		Empty,
		Camera,
		Light,
		Mesh,
		Bone,
		TypeMax
	};

	GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;

	HashingString GetHashedName() const { return m_name; }
    void SetName(const std::string_view& name) { m_name = name.data(); }

	template<typename T>
	T* AddComponent();

	template<typename T>
	T* AddScriptComponent(const std::string_view& scriptName);

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args);

	template<typename T>
	T* GetComponent(uint32 id);

	template<typename T>
	T* GetComponent();

	template<typename T>
	std::vector<T*> GetComponents();

	template<typename T>
	void RemoveComponent(T* component);

	GameObject::Type GetType() const { return m_gameObjectType; }

    static GameObject* Find(const std::string_view& name);

	Transform m_transform{};
	GameObject::Index m_index;
	GameObject::Index m_parentIndex;
	//for bone update
	GameObject::Index m_rootIndex{ 0 };
	std::vector<GameObject::Index> m_childrenIndices;

    ReflectionFieldInheritance(GameObject, PropertyOnly, Object)
    {
        PropertyField
        ({
            meta_property(m_name)
            meta_property(m_index)
            meta_property(m_parentIndex)
            meta_property(m_rootIndex)
            meta_property(m_childrenIndices)
        });

        ReturnReflectionInheritancePropertyOnly(GameObject)
    }

private:
	friend class RenderScene;
	friend class ModelLoader;
	friend class HotLoadSystem;

	void ComponentsSort();

private:
	GameObject::Type m_gameObjectType{ GameObject::Type::Empty };
	
	const size_t m_typeID{ TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	const size_t m_instanceID{ TypeTrait::GUIDCreator::GetGUID() };
	
	HashingString m_tag{};
	
	std::unordered_map<uint32_t, size_t> m_componentIds{};
	std::vector<Component*> m_components{};

	//debug layer
	Bone* selectedBone{ nullptr };
};

#include "GameObejct.inl"


