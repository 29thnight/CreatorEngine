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

	GameObject() = default;
	GameObject(const std::string_view& name, GameObject::Type type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;
	~GameObject() override = default;

	HashingString GetHashedName() const { return m_name; }
    void SetName(const std::string_view& name) { m_name = name.data(); }

	void AddComponent(Meta::Type& type);

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

	void RemoveComponent(uint32 id);

	void RemoveComponent(const std::string_view& scriptName);

	void RemoveComponent(Meta::Type& type);

	GameObject::Type GetType() const { return m_gameObjectType; }

    static GameObject* Find(const std::string_view& name);

	Transform m_transform{};
	GameObject::Index m_index;
	GameObject::Index m_parentIndex;
	//for bone update
	GameObject::Index m_rootIndex{ 0 };
	std::vector<GameObject::Index> m_childrenIndices;

    ReflectionFieldInheritance(GameObject, Object)
    {
        PropertyField
        ({
            meta_property(m_index)
            meta_property(m_parentIndex)
            meta_property(m_rootIndex)
            meta_property(m_childrenIndices)
			meta_property(m_components)
        });

        FieldEnd(GameObject, PropertyOnlyInheritance)
    }

	friend class RenderScene;
	friend class ModelLoader;
	friend class HotLoadSystem;

private:
	GameObject::Type m_gameObjectType{ GameObject::Type::Empty };
	
	HashedGuid m_typeID{ TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	HashedGuid m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
	
	HashingString m_tag{};
	
	std::unordered_map<HashedGuid, size_t> m_componentIds{};
	std::vector<std::shared_ptr<Component>> m_components{};

	//debug layer
	Bone* selectedBone{ nullptr };
};

#include "GameObejct.inl"


