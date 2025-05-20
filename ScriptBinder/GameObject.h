#pragma once
#include "HotLoadSystem.h"
#include "Object.h"
#include "Component.h"
#include "Transform.h"
#include "GameObjectType.h"
#include "GameObject.generated.h"

class Scene;
class Bone;
class RenderScene;
class ModelLoader;
class ModuleBehavior;
class GameObject : public Object
{
public:
	[[property]]
	float speed = 0; //***** 이거 언제까지 유지해야함?
	using Index = int;
	static constexpr GameObject::Index INVALID_INDEX = std::numeric_limits<uint32_t>::max();
	enum class Type
	{
		Empty,
		Camera,
		Light,
		Mesh,
		Bone,
		TypeMax
	};
    ReflectGameObject
    [[Serializable(Inheritance:Object)]]
	GameObject();
	GameObject(const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(size_t instanceID, const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex);
	GameObject(GameObject&) = delete;
	GameObject(GameObject&&) noexcept = default;
	GameObject& operator=(GameObject&) = delete;
	~GameObject() override = default;

	HashingString GetHashedName() const { return m_name; }
    void SetName(const std::string_view& name) { m_name = name.data(); }
	void SetTag(const std::string_view& tag);

	virtual void Destroy() override final;

	std::shared_ptr<Component> AddComponent(const Meta::Type& type);
	ModuleBehavior* AddScriptComponent(const std::string_view& scriptName);

    std::shared_ptr<Component> GetComponent(const Meta::Type& type);

	template<typename T>
	T* AddComponent();

	template<typename T, typename... Args>
	T* AddComponent(Args&&... args);

	template<typename T>
	T* GetComponent(uint32 id);

	template<typename T>
	T* GetComponent();

	template<typename T>
	bool HasComponent();

	template<typename T>
	std::vector<T*> GetComponents();

	template<typename T>
	void RemoveComponent(T* component);

	void RemoveComponentIndex(uint32 id);

	void RemoveComponentTypeID(uint32 typeID);

	void RemoveScriptComponent(const std::string_view& scriptName);

	void RemoveComponent(Meta::Type& type);

	GameObjectType GetType() const { return m_gameObjectType; }

    static GameObject* Find(const std::string_view& name);
	static GameObject* FindIndex(GameObject::Index index);

	static inline bool IsValidIndex(Index index)
	{
		return index != INVALID_INDEX;
	}

	static inline bool IsInvalidIndex(Index index)
	{
		return index == INVALID_INDEX;
	}

	void SetEnabled(bool able) override final;

    [[Property]]
	Transform m_transform{};
    [[Property]]
	GameObject::Index m_index{ INVALID_INDEX };
    [[Property]]
	GameObject::Index m_parentIndex{ INVALID_INDEX };
	//for bone update
    [[Property]]
	GameObject::Index m_rootIndex{ 0 };
    [[Property]]
	std::vector<GameObject::Index> m_childrenIndices;

public:
	[[Property]]
	GameObjectType m_gameObjectType{ GameObjectType::Empty };
	HashedGuid m_instanceID{ TypeTrait::GUIDCreator::MakeGUID() };
	[[Property]]
	HashingString m_tag{ "Untagged" };
	
	std::unordered_map<HashedGuid, size_t> m_componentIds{};
    [[Property]]
	std::vector<std::shared_ptr<Component>> m_components{};
};

#include "GameObejct.inl"


